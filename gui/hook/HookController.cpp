#include "hook/HookController.h"

#include "common/PathUtils.h"
#include "common/PdbCache.h"
#include "common/RegistryUtils.h"
#include "common/Win32Error.h"

#if defined(TOUCHREV_BUILD_BLOCKER)
#include "injector/inject.h"
#include "injector/process_find.h"
#include "common/winutil.h"
#endif

#include <memory>

namespace touchrev::hook
{
namespace
{
constexpr UINT kCompletionMessage = WM_APP + 1;
constexpr UINT_PTR kRecoveryTimerId = 1;
constexpr UINT kRecoveryInitialDelayMs = 3000;
constexpr UINT kRecoveryRetryDelayMs = 2000;
constexpr int kRecoveryRetries = 5;

constexpr wchar_t kHookDllName[] = L"TouchRevBlockerHook.dll";
constexpr wchar_t kStatusKey[] = L"Software\\Touch-Rev";

// 数值与 blocker/common/blocker_status.h 保持一致。
constexpr DWORD kPdbStatusDownloadFailed = 3;
constexpr DWORD kHookStatusSymbolResolveFailed = 2;
constexpr DWORD kHookStatusWriteFailed = 5;

std::wstring HookDllPath()
{
    return touchrev::common::ModuleRelativePath(kHookDllName);
}

bool IsBlockerInFailedState()
{
    namespace registry = touchrev::common::registry;
    const DWORD pdbStatus = registry::ReadDword(HKEY_CURRENT_USER, kStatusKey, L"PdbStatus", 0);
    const DWORD hookStatus = registry::ReadDword(HKEY_CURRENT_USER, kStatusKey, L"HookStatus", 0);
    return pdbStatus == kPdbStatusDownloadFailed ||
           (hookStatus >= kHookStatusSymbolResolveFailed && hookStatus <= kHookStatusWriteFailed);
}

void ClearCacheFiles(OperationResult& result)
{
    const auto clear = touchrev::common::pdbcache::Clear();
    touchrev::common::pdbcache::ResetStatus();
    result.succeeded = clear.succeeded;
    if (clear.succeeded)
    {
        result.message = L"已清除 PDB 符号缓存（" + std::to_wstring(clear.deletedFiles) + L" 个文件）。";
    }
    else
    {
        result.message = L"部分符号文件被占用，已登记为重启后删除（剩余 " +
                         std::to_wstring(clear.lockedFiles) + L" 个）。";
    }
}

#if defined(TOUCHREV_BUILD_BLOCKER)
struct ExplorerTarget
{
    DWORD pid = 0;
    std::wstring dllPath;
};

// 注入前的全部校验：DLL 存在、架构与当前构建一致、找到 explorer 且架构匹配。
bool PrepareTarget(ExplorerTarget& target, std::wstring& error)
{
    target.dllPath = HookDllPath();

    const DWORD attributes = GetFileAttributesW(target.dllPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        error = L"找不到 Hook DLL 文件:\n" + target.dllPath;
        return false;
    }

    USHORT dllMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    if (!touchrev::GetPeMachineType(target.dllPath, &dllMachine))
    {
        error = L"读取 DLL PE 机器类型失败。";
        return false;
    }

    if (dllMachine != touchrev::CurrentBuildMachineType())
    {
        error = L"DLL 架构与当前构建架构不匹配。\n预期: " + std::wstring(touchrev::CurrentBuildArchName()) +
                L"\n实际: " + touchrev::MachineTypeToString(dllMachine);
        return false;
    }

    std::wstring detail;
    if (!touchrev::IsCurrentProcessNativeForCurrentBuild(&detail))
    {
        error = L"当前进程校验失败:\n" + detail;
        return false;
    }

    touchrev::TargetProcess process;
    if (!touchrev::FindShellExplorer(&process, &detail))
    {
        error = L"寻找资源管理器失败:\n" + detail;
        return false;
    }

    if (!touchrev::IsNativeMachineForCurrentBuild(process.processMachine, process.nativeMachine))
    {
        error = L"目标 explorer.exe 架构与当前构建架构不匹配。\n预期: " +
                std::wstring(touchrev::CurrentBuildArchName()) +
                L"\n进程架构: " + touchrev::MachineTypeToString(process.processMachine) +
                L"\n原生架构: " + touchrev::MachineTypeToString(process.nativeMachine);
        return false;
    }

    target.pid = process.pid;
    return true;
}

bool QueryLoaded(const ExplorerTarget& target, bool& loaded, HMODULE* module, std::wstring& error)
{
    touchrev::RemoteModuleInfo info;
    loaded = false;
    if (!touchrev::QueryRemoteDllModule(target.pid, target.dllPath, &info, &error))
    {
        return false;
    }
    loaded = info.module != nullptr;
    if (module)
    {
        *module = info.module;
    }
    return true;
}

bool EnableIn(const ExplorerTarget& target, std::wstring& error)
{
    bool loaded = false;
    HMODULE module = nullptr;
    if (!QueryLoaded(target, loaded, &module, error))
    {
        return false;
    }

    // DLL 已加载时仍显式发送 Start，以支持幂等启动和失败后的重试。
    if (loaded)
    {
        return touchrev::StartHookInProcess(target.pid, target.dllPath, module, &error);
    }

    HMODULE remoteModule = nullptr;
    return touchrev::InjectDllIntoProcess(target.pid, target.dllPath, &remoteModule, &error);
}

bool DisableIn(const ExplorerTarget& target, bool& deferred, std::wstring& error)
{
    touchrev::RemoteModuleInfo unloadedModule;
    DWORD freeLibraryResult = 0;
    return touchrev::UninstallDllFromProcess(
        target.pid, target.dllPath, &unloadedModule, &freeLibraryResult, &error, &deferred);
}
#endif
}

bool HookController::Initialize(HINSTANCE instance, CompletionCallback onCompleted)
{
    instance_ = instance;
    onCompleted_ = std::move(onCompleted);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = HookController::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = HookController::WindowClassName;
    RegisterClassExW(&windowClass);

    hwnd_ = CreateWindowExW(0, HookController::WindowClassName, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, instance_, this);
    if (!hwnd_)
    {
        DebugLogHResult(L"CreateWindowExW(HookController)", HResultFromLastError());
        return false;
    }
    return true;
}

void HookController::Shutdown(bool uninstall)
{
    if (hwnd_)
    {
        KillTimer(hwnd_, kRecoveryTimerId);
    }
    recovering_ = false;

    if (worker_.joinable())
    {
        worker_.join();
    }
    DrainPendingResults();
    busy_ = false;

    if (uninstall)
    {
        // 退出时恢复系统默认手势，并释放 explorer 对 DLL 文件的占用。
        const OperationResult result = Execute(Operation::Disable, false);
        if (!result.succeeded && !result.message.empty())
        {
            DebugLog(L"[HookController] uninstall on exit failed: " + result.message);
        }
    }

    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool HookController::RequestEnable(bool userInitiated)
{
    if (!StartOperation(Operation::Enable, userInitiated))
    {
        return false;
    }
    desired_ = true;
    return true;
}

bool HookController::RequestDisable(bool userInitiated)
{
    if (!StartOperation(Operation::Disable, userInitiated))
    {
        return false;
    }
    desired_ = false;
    recovering_ = false;
    return true;
}

bool HookController::RequestClearCache(bool userInitiated)
{
    return StartOperation(Operation::ClearCache, userInitiated);
}

bool HookController::IsModuleLoaded()
{
    const DWORD pid = QueryExplorerPid();
    if (pid == 0)
    {
        cachedPid_ = 0;
        cachedLoaded_ = false;
        return false;
    }

    // 模块只会因我们的操作或 explorer 重启而变化：pid 不变就沿用缓存，
    // 只有延后卸载尚未完成时需要持续确认。
    if (pid != cachedPid_ || deferredUnloadPending_)
    {
#if defined(TOUCHREV_BUILD_BLOCKER)
        touchrev::RemoteModuleInfo info;
        std::wstring error;
        cachedLoaded_ = touchrev::QueryRemoteDllModule(pid, HookDllPath(), &info, &error) && info.module != nullptr;
#else
        cachedLoaded_ = false;
#endif
        cachedPid_ = pid;
        if (!cachedLoaded_)
        {
            deferredUnloadPending_ = false;
        }
    }
    return cachedLoaded_;
}

bool HookController::IsEffective()
{
    if (deferredUnloadPending_)
    {
        return false;
    }
    return IsModuleLoaded() && !IsBlockerInFailedState();
}

void HookController::ApplyStartupPolicy(bool autoInstall)
{
    desired_ = IsModuleLoaded();
    if (autoInstall && !IsEffective())
    {
        RequestEnable(false);
    }
}

void HookController::OnExplorerRestarted()
{
    cachedPid_ = 0;
    deferredUnloadPending_ = false;
    if (!desired_)
    {
        return;
    }

    recovering_ = true;
    recoveryRetriesLeft_ = kRecoveryRetries;
    // 新 explorer 需要一点时间加载 twinui.pcshell，立刻注入会因找不到符号模块而失败。
    ScheduleRecovery(kRecoveryInitialDelayMs);
}

LRESULT CALLBACK HookController::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* controller = static_cast<HookController*>(createStruct->lpCreateParams);
        // CreateWindowExW 返回前 hwnd_ 仍为空，HandleMessage 里的 DefWindowProcW(hwnd_) 必须拿到有效句柄，
        // 否则 WM_NCCREATE 返回 FALSE，窗口创建失败。
        controller->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(controller));
    }

    auto* controller = reinterpret_cast<HookController*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (controller)
    {
        return controller->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT HookController::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case kCompletionMessage:
        OnOperationCompleted(reinterpret_cast<OperationResult*>(lParam));
        return 0;

    case WM_TIMER:
        if (wParam == kRecoveryTimerId)
        {
            KillTimer(hwnd_, kRecoveryTimerId);
            if (recovering_ && !StartOperation(Operation::Enable, false))
            {
                ScheduleRecovery(kRecoveryRetryDelayMs);
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

bool HookController::StartOperation(Operation operation, bool userInitiated)
{
    if (busy_ || !hwnd_)
    {
        return false;
    }

    if (worker_.joinable())
    {
        worker_.join();
    }

    busy_ = true;
    const HWND notify = hwnd_;
    worker_ = std::thread([notify, operation, userInitiated]() {
        auto* result = new OperationResult(Execute(operation, userInitiated));
        if (!PostMessageW(notify, kCompletionMessage, 0, reinterpret_cast<LPARAM>(result)))
        {
            delete result;
        }
    });
    return true;
}

void HookController::OnOperationCompleted(OperationResult* rawResult)
{
    std::unique_ptr<OperationResult> result(rawResult);
    if (worker_.joinable())
    {
        worker_.join();
    }
    busy_ = false;

    cachedPid_ = result->explorerPid;
    cachedLoaded_ = result->moduleLoaded;
    deferredUnloadPending_ = result->stopDeferred && result->moduleLoaded;

    if (recovering_ && result->operation == Operation::Enable)
    {
        if (result->succeeded)
        {
            recovering_ = false;
        }
        else if (recoveryRetriesLeft_-- > 0)
        {
            // 还会重试，不打扰用户。
            result->message.clear();
            ScheduleRecovery(kRecoveryRetryDelayMs);
        }
        else
        {
            recovering_ = false;
            result->message = L"资源管理器重启后未能恢复拦截：\n" + result->message;
        }
    }

    if (onCompleted_)
    {
        onCompleted_(*result);
    }
}

void HookController::ScheduleRecovery(UINT delayMs)
{
    if (hwnd_)
    {
        SetTimer(hwnd_, kRecoveryTimerId, delayMs, nullptr);
    }
}

void HookController::DrainPendingResults()
{
    if (!hwnd_)
    {
        return;
    }

    MSG message{};
    while (PeekMessageW(&message, hwnd_, kCompletionMessage, kCompletionMessage, PM_REMOVE))
    {
        delete reinterpret_cast<OperationResult*>(message.lParam);
    }
}

DWORD HookController::QueryExplorerPid()
{
    HWND shellTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!shellTray)
    {
        return 0;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(shellTray, &pid);
    return pid;
}

OperationResult HookController::Execute(Operation operation, bool userInitiated)
{
    OperationResult result{.operation = operation, .userInitiated = userInitiated};

#if defined(TOUCHREV_BUILD_BLOCKER)
    ExplorerTarget target;
    std::wstring error;
    if (!PrepareTarget(target, error))
    {
        // explorer 不可用不妨碍清理本地缓存。
        if (operation == Operation::ClearCache)
        {
            ClearCacheFiles(result);
        }
        else
        {
            result.message = error;
        }
        return result;
    }
    result.explorerPid = target.pid;

    switch (operation)
    {
    case Operation::Enable:
        result.succeeded = EnableIn(target, error);
        if (!result.succeeded)
        {
            result.message = L"启用拦截失败:\n" + error;
        }
        break;

    case Operation::Disable:
        result.succeeded = DisableIn(target, result.stopDeferred, error);
        if (!result.succeeded)
        {
            result.message = L"关闭拦截失败:\n" + error;
        }
        else if (result.stopDeferred)
        {
            result.message = L"拦截模块仍在下载符号，完成后会自动恢复系统手势并卸载。";
        }
        break;

    case Operation::ClearCache:
    {
        bool wasLoaded = false;
        QueryLoaded(target, wasLoaded, nullptr, error);
        if (wasLoaded)
        {
            // explorer 内的 dbghelp 持有 PDB 句柄，必须先关闭拦截才能真正删掉文件。
            if (!DisableIn(target, result.stopDeferred, error))
            {
                result.message = L"清除前关闭拦截失败:\n" + error;
                break;
            }
            if (result.stopDeferred)
            {
                result.message = L"拦截模块正在下载符号，暂时无法释放缓存，请稍后再试。";
                break;
            }
        }

        ClearCacheFiles(result);

        if (wasLoaded && !EnableIn(target, error))
        {
            result.succeeded = false;
            result.message += L"\n重新启用拦截失败:\n" + error;
        }
        break;
    }
    }

    bool loaded = false;
    std::wstring queryError;
    if (QueryLoaded(target, loaded, nullptr, queryError))
    {
        result.moduleLoaded = loaded;
    }
#else
    if (operation == Operation::ClearCache)
    {
        ClearCacheFiles(result);
    }
    else
    {
        result.message = L"未启用 Blocker 构建，无法执行 Hook 操作。";
    }
#endif

    return result;
}
}
