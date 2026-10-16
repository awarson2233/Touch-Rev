#pragma once

#include "DragProxyWindow.h"
#include "ThinXamlAppSwitcherHost.h"
#include "ui/shared/ThemeManager.h"
#include "MainView.h"
#include "common/CoordinateSpace.h"
#include "input/InputController.h"

#include <windows.h>

#include <functional>

class MainWindow
{
public:
    enum class ActivationCommand : WPARAM
    {
        Show = 1,
        Hide = 2,
        Toggle = 3,
        Exit = 4,
    };

    static constexpr wchar_t WindowClassName[] = L"TouchRevGUI.MainWindow";
    static constexpr wchar_t WakeMessageName[] = L"TouchRevGUI.Wake";
    static constexpr wchar_t SingleInstanceMutexName[] = L"Local\\TouchRevGUI.SingleInstance";

    bool Initialize(HINSTANCE instance, int showCommand);
    int Run();
    // 唤醒消息要显示/隐藏配置窗口时回调宿主，避免这里直接操作另一个窗口的内部状态。
    void SetConfigVisibilityCallback(std::function<void(bool)> callback);
    void ShowSwitcher(const POINT* touchCenter = nullptr);
    void Hide();
    void ToggleSwitcher();
    void ExitApplication();
    bool IsSwitcherVisible() const { return isVisible_; }

private:
    struct ActivationResult
    {
        bool foreground = false;
        bool directSucceeded = false;
        HWND actualForeground = nullptr;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    HRESULT OnCreate();
    void OnDestroy();
    void OnSize(UINT width, UINT height);
    void OnDpiChanged(WPARAM wParam, LPARAM lParam);
    void OnPaint();
    void RefreshTheme();
    void ApplyActivationVisualState(bool active);
    ActivationResult TryActivateSwitcher();
    void SyncClientLayout(UINT width, UINT height, bool renderSwitcher);
    void RebaseActiveDrag();
    // 按识别器是否有进行中的会话启停手势定时器。
    void UpdateGestureTimer();
    // 由目标显示器的 rcMonitor 与 rcWork 之差得出任务栏占用的四边，交给卡片排版避让。
    void ApplyWorkAreaInsets();
    void ForwardDpiChangeToChild(WPARAM wParam, LPARAM lParam);
    void HandleInputResult(const InputController::Result& result);
    // 首帧闸门：布局提交完成后才解除 cloak，避免呼出瞬间看到旧布局跳变。
    void BeginRevealAfterFirstFrame();
    void RevealNow();
    void CancelPendingReveal();
    void PrewarmSwitcher();
    // 卡片拖出抓起阈值后把拖动交给代理窗口：显示代理、隐藏切换器，再异步启动系统移动循环。
    bool HandOffDragToProxy(const touchrev::appswitcher::MainView::ItemDragOut& dragOut);
    void StartPendingWindowDrag();
    // 把代理拿到的系统排列结果搬到真实窗口上。
    void ApplyDragProxyResult(const touchrev::appswitcher::DragProxyWindow::Result& result);
    // 取消拖动期间对真实窗口的 shell 隐藏。
    void ReleaseTargetCloak();
    void DispatchGestureAction(const InputController::RawInputResult& inputResult);
    bool HandleKeyDown(WPARAM key);
    HMONITOR ResolveTargetMonitor(const POINT* touchCenter = nullptr) const;
    MONITORINFO LoadMonitorInfo(HMONITOR monitor) const;

    bool CanStartDragFromPointer(WPARAM wParam) const;
    bool CanStartDragFromMouse(LPARAM lParam) const;

    static UINT ClientWidthFromLParam(LPARAM lParam);
    static UINT ClientHeightFromLParam(LPARAM lParam);
    static UINT GetClientWidth(HWND hwnd);
    static UINT GetClientHeight(HWND hwnd);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HMONITOR targetMonitor_ = nullptr;
    RECT targetMonitorRectPx_{};
    RECT targetWorkAreaPx_{};
    float dpi_ = 96.0f;
    UINT wakeMessage_ = 0;
    bool isVisible_ = false;
    bool isExiting_ = false;

    CoordinateSpace coordinates_;
    InputController inputController_;
    ThemeManager themeManager_;
    ThinXamlAppSwitcherHost xamlHost_;
    touchrev::appswitcher::MainView appSwitcherMainView_;
    bool isSyncingLayout_ = false;
    bool isLongPressNavigating_ = false;
    bool pendingLongPressActivation_ = false;
    bool activationSucceeded_ = false;
    bool keyboardNavigationEnabled_ = false;
    std::function<void(bool)> configVisibilityCallback_;
    bool isPrewarmed_ = false;
    bool gestureTimerActive_ = false;
    bool gestureInputSuspended_ = false;
    touchrev::appswitcher::DragProxyWindow dragProxy_;
    HWND shellCloakedTarget_ = nullptr;
    bool revealPending_ = false;
    int pendingRevealFrames_ = 0;
    winrt::event_token renderingToken_{};
};
