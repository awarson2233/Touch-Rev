#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <thread>

namespace touchrev::hook
{
enum class Operation
{
    Enable,
    Disable,
    ClearCache,
};

struct OperationResult
{
    Operation operation = Operation::Enable;
    bool userInitiated = false;
    bool succeeded = false;
    // 操作结束时 DLL 是否仍加载在 explorer 中，以及当时的 explorer pid，用于刷新状态缓存。
    bool moduleLoaded = false;
    DWORD explorerPid = 0;
    // 延后停止：Hook 仍在下载符号，会在初始化结束后自行恢复手势表并卸载。
    bool stopDeferred = false;
    // 面向用户的结果或失败原因；为空表示无需提示。
    std::wstring message;
};

// 管理 explorer 内拦截模块的整个生命周期。
// 跨进程操作（注入、启动、停止、卸载）可能耗时数十秒，全部在工作线程执行，完成后回到 UI 线程回调；
// 同时负责 explorer 重启后的自动恢复，以及应用退出时的卸载。
class HookController
{
public:
    static constexpr wchar_t WindowClassName[] = L"TouchRevGUI.HookController";
    using CompletionCallback = std::function<void(const OperationResult&)>;

    bool Initialize(HINSTANCE instance, CompletionCallback onCompleted);
    // 等待进行中的操作结束；uninstall 为真时同步卸载拦截模块，让系统手势在退出后恢复。
    void Shutdown(bool uninstall);

    // 已有操作在执行时返回 false。
    bool RequestEnable(bool userInitiated);
    bool RequestDisable(bool userInitiated);
    // 清除 PDB 缓存：模块已加载时先卸载、清理，再按原状态重新启用。
    bool RequestClearCache(bool userInitiated);
    bool IsBusy() const { return busy_; }

    // DLL 是否加载在当前 explorer 中。按 explorer pid 缓存，pid 不变时不再做模块快照。
    bool IsModuleLoaded();
    // 已加载且未处于失败状态（PDB 下载失败、符号/表项校验失败、写入失败）。开关以此为准。
    bool IsEffective();

    // 启动时调用：记录当前是否已有拦截，并按设置决定是否自动启用。
    void ApplyStartupPolicy(bool autoInstall);
    // 托盘收到 TaskbarCreated 时调用：explorer 重启会丢掉拦截，按用户意图延迟重新注入。
    void OnExplorerRestarted();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool StartOperation(Operation operation, bool userInitiated);
    void OnOperationCompleted(OperationResult* result);
    void ScheduleRecovery(UINT delayMs);
    void DrainPendingResults();

    static OperationResult Execute(Operation operation, bool userInitiated);
    static DWORD QueryExplorerPid();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    CompletionCallback onCompleted_;
    std::thread worker_;
    bool busy_ = false;

    // 用户意图：最近一次请求的是启用还是关闭，explorer 重启后据此恢复。
    bool desired_ = false;
    bool recovering_ = false;
    int recoveryRetriesLeft_ = 0;

    DWORD cachedPid_ = 0;
    bool cachedLoaded_ = false;
    // 延后停止已登记、DLL 尚未自行卸载：期间持续确认模块状态，开关按"已关闭"显示。
    bool deferredUnloadPending_ = false;
};
}
