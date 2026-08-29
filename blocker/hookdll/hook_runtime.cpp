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

std::mutex g_operationMutex;
std::mutex g_stateMutex;
RuntimeState g_state = RuntimeState::Stopped;
HANDLE g_initializeThread = nullptr;
bool g_installSucceeded = false;

DWORD WINAPI InitializeHookThread(LPVOID) {
    const bool installed = touchrev::InstallTwinuiGestureTablePatch();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_installSucceeded = installed;
        if (g_state != RuntimeState::Stopping) {
            g_state = installed ? RuntimeState::Running : RuntimeState::Failed;
        }
    }

    touchrev::LogMessage(
        L"hookdll", installed ? touchrev::LogLevel::Info : touchrev::LogLevel::Error,
        installed ? L"event=READY" : L"event=READY_FAILED");
    return installed ? ERROR_SUCCESS : ERROR_FUNCTION_FAILED;
}

void SetStoppedState() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_state = RuntimeState::Stopped;
    g_installSucceeded = false;
}

}  // namespace

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
        if (g_state == RuntimeState::Failed) {
            return ERROR_INVALID_STATE;
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
            return ERROR_BUSY;
        }

        g_state = RuntimeState::Stopping;
        initializeThread = g_initializeThread;
    }

    touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                         L"event=STOP_REQUEST initializeThread=0x%p",
                         initializeThread);

    if (initializeThread) {
        const DWORD wait = WaitForSingleObject(initializeThread, INFINITE);
        if (wait != WAIT_OBJECT_0) {
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
    touchrev::LogMessage(L"hookdll", touchrev::LogLevel::Info,
                         L"event=STOP_COMPLETE installed=%d",
                         g_installSucceeded ? 1 : 0);

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
