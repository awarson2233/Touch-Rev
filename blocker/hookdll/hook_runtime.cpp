#include "hookdll/hook_runtime.h"

#include "common/blocker_status.h"
#include "common/log.h"
#include "common/winutil.h"
#include "hookdll/dbghelp_symbol_provider.h"
#include "hookdll/twinui_gesture_table_patch.h"

#include <mutex>

namespace {

enum class RuntimeState {
    Stopped,
    Starting,
    Running,
    Failed,
    Stopping,
};

// Stop 等待初始化线程的上限。PDB 首次下载可能耗时数十秒，不能让注入方的远程线程无限期挂住。
constexpr DWORD kStopWaitTimeoutMs = 3000;

std::mutex g_operationMutex;
std::mutex g_stateMutex;
RuntimeState g_state = RuntimeState::Stopped;
HANDLE g_initializeThread = nullptr;
bool g_installSucceeded = false;
// 以下两个标志只在 g_stateMutex 内读写：初始化线程与 Stop 由它决定由谁负责收尾。
bool g_initializeDone = false;
bool g_stopDeferred = false;
HMODULE g_module = nullptr;

// 延后停止：初始化线程收尾后恢复手势表，并带走注入时 LoadLibrary 的那一份引用。
void CompleteDeferredStop() {
    const bool restored = touchrev::UninstallTwinuiGestureTablePatch();
    if (!restored) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state = RuntimeState::Failed;
        g_stopDeferred = false;
        touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Error,
                             L"event=DEFERRED_STOP_RESTORE_FAILED");
        return;
    }

    touchrev::ShutdownDbgHelpSession();
    touchrev::ResetBlockerStatus();
    touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                         L"event=DEFERRED_STOP_COMPLETE");
    touchrev::ShutdownLog();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_initializeThread) {
            CloseHandle(g_initializeThread);
            g_initializeThread = nullptr;
        }
        g_state = RuntimeState::Stopped;
        g_installSucceeded = false;
        g_stopDeferred = false;
    }

    if (g_module) {
        FreeLibraryAndExitThread(g_module, ERROR_SUCCESS);
    }
}

DWORD WINAPI InitializeHookThread(LPVOID) {
    const bool installed = touchrev::InstallTwinuiGestureTablePatch();

    bool stopDeferred = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_installSucceeded = installed;
        g_initializeDone = true;
        stopDeferred = g_stopDeferred;
        if (g_state != RuntimeState::Stopping) {
            g_state = installed ? RuntimeState::Running : RuntimeState::Failed;
        }
    }

    touchrev::LogMessage(
        L"hookdll", installed ? touchrev::LogLevel::Info : touchrev::LogLevel::Error,
        installed ? L"event=READY" : L"event=READY_FAILED");

    if (stopDeferred) {
        CompleteDeferredStop();
    }
    return installed ? ERROR_SUCCESS : ERROR_FUNCTION_FAILED;
}

void SetStoppedState() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_state = RuntimeState::Stopped;
    g_installSucceeded = false;
}

}  // namespace

namespace touchrev {
void SetHookModule(HMODULE module) {
    g_module = module;
}
}  // namespace touchrev

extern "C" __declspec(dllexport) DWORD WINAPI TouchRevHookStart(LPVOID) {
    std::lock_guard<std::mutex> operationLock(g_operationMutex);

    {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        if (g_state == RuntimeState::Starting || g_state == RuntimeState::Running) {
            return ERROR_SUCCESS;
        }
        if (g_state == RuntimeState::Stopping) {
            return ERROR_BUSY;
        }
        // Failed 允许直接重试（例如清除损坏的 PDB 缓存后），安装失败时手势表已整体回滚。
        if (g_initializeThread) {
            CloseHandle(g_initializeThread);
            g_initializeThread = nullptr;
        }
    }

    touchrev::InitializeLog(L"hookdll");
    touchrev::ResetBlockerStatus();
    touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                         L"event=START_REQUEST arch=%s",
                         touchrev::CurrentBuildArchName());

    {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        g_state = RuntimeState::Starting;
        g_installSucceeded = false;
        g_initializeDone = false;
        g_stopDeferred = false;
    }

    HANDLE thread = CreateThread(nullptr, 0, InitializeHookThread, nullptr, 0, nullptr);
    if (!thread) {
        const DWORD error = GetLastError();
        SetStoppedState();
        touchrev::UpdateBlockerHookStatus(
            touchrev::BlockerHookStatus::WriteFailed, error,
            L"HOOK_INITIALIZE_THREAD_CREATE_FAILED");
        touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Error,
                             L"event=START_FAILED error=%lu message=%s", error,
                             touchrev::FormatLastError(error).c_str());
        touchrev::ShutdownLog();
        return error;
    }

    {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        g_initializeThread = thread;
    }

    touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                         L"event=START_ACCEPTED thread=0x%p", thread);
    return ERROR_SUCCESS;
}

extern "C" __declspec(dllexport) DWORD WINAPI TouchRevHookStop(LPVOID) {
    std::lock_guard<std::mutex> operationLock(g_operationMutex);

    HANDLE initializeThread = nullptr;
    {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        if (g_state == RuntimeState::Stopped) {
            return ERROR_SUCCESS;
        }
        if (g_state == RuntimeState::Stopping) {
            return g_stopDeferred ? ERROR_IO_PENDING : ERROR_BUSY;
        }

        g_state = RuntimeState::Stopping;
        initializeThread = g_initializeThread;
    }

    touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                         L"event=STOP_REQUEST initializeThread=0x%p",
                         initializeThread);

    if (initializeThread) {
        const DWORD wait = WaitForSingleObject(initializeThread, kStopWaitTimeoutMs);
        if (wait == WAIT_TIMEOUT) {
            {
                std::lock_guard<std::mutex> stateLock(g_stateMutex);
                if (!g_initializeDone) {
                    g_stopDeferred = true;
                    touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                                         L"event=STOP_DEFERRED reason=initialize-running");
                    return ERROR_IO_PENDING;
                }
            }
            // 初始化恰好在超时后结束：它没有看到延后标志，只剩日志收尾，等它真正退出后
            // 再由这里正常停止，否则注入方随后的 FreeLibrary 会卸掉仍在执行的代码。
            WaitForSingleObject(initializeThread, INFINITE);
        } else if (wait != WAIT_OBJECT_0) {
            const DWORD error = wait == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
            {
                std::lock_guard<std::mutex> stateLock(g_stateMutex);
                g_state = RuntimeState::Failed;
            }
            touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Error,
                                 L"event=STOP_WAIT_FAILED error=%lu message=%s",
                                 error, touchrev::FormatLastError(error).c_str());
            return error;
        }
    }

    const bool restored = touchrev::UninstallTwinuiGestureTablePatch();
    if (!restored) {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        g_state = RuntimeState::Failed;
        return ERROR_WRITE_FAULT;
    }

    touchrev::ShutdownDbgHelpSession();
    touchrev::ResetBlockerStatus();

    bool installSucceeded = false;
    {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        installSucceeded = g_installSucceeded;
    }
    touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                         L"event=STOP_COMPLETE installed=%d",
                         installSucceeded ? 1 : 0);

    {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        if (g_initializeThread) {
            CloseHandle(g_initializeThread);
            g_initializeThread = nullptr;
        }
        g_state = RuntimeState::Stopped;
        g_installSucceeded = false;
    }

    touchrev::ShutdownLog();
    return ERROR_SUCCESS;
}
