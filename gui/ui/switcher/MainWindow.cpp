#include "MainWindow.h"

#include "WindowController.h"
#include "common/Win32Error.h"
#include "common/AppSettings.h"
#include "common/DwmUtils.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <dwmapi.h>
#include <sstream>
#include <utility>
#include <vector>
#include <windowsx.h>

namespace
{
constexpr wchar_t kWindowTitle[] = L"Touch Rev GUI";

// 与 RawTouchInputViewer 对齐：定时推进三指长按状态机（静止时硬件不持续上报帧）。
constexpr UINT_PTR kGestureTimerId = 1;
constexpr UINT kGestureTimerMs = 16;

// 首帧闸门的兜底定时器：渲染回调迟迟不来时也必须把窗口揭示出去。
constexpr UINT_PTR kRevealFallbackTimerId = 2;
constexpr UINT kRevealFallbackMs = 120;

// 启动后预热切换器视觉树（解析卡片模板、建立缩略图），让首次呼出走热路径。
constexpr UINT kPrewarmMessage = WM_APP + 1;

// 卡片拖出后启动目标窗口的系统移动循环；放到消息队列里做，不在 XAML 指针回调中进入模态循环。
constexpr UINT kStartWindowDragMessage = WM_APP + 2;

// 空闲时预备拖动代理（窗口、XAML 岛、卡片模板），拖出卡片时不再现场创建。
constexpr UINT kPrepareDragProxyMessage = WM_APP + 3;

#if defined(WM_DWMCOMPOSITIONCHANGED)
constexpr UINT kWmDwmCompositionChanged = WM_DWMCOMPOSITIONCHANGED;
#else
constexpr UINT kWmDwmCompositionChanged = 0x031E;
#endif



float GetMonitorDpi(HMONITOR monitor)
{
    typedef HRESULT(WINAPI* GetDpiForMonitorProc)(HMONITOR, int, UINT*, UINT*);
    static const auto getDpiForMonitor = []() -> GetDpiForMonitorProc {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        return shcore ? reinterpret_cast<GetDpiForMonitorProc>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
    }();

    UINT dpiX = 96;
    UINT dpiY = 96;
    if (getDpiForMonitor)
    {
        getDpiForMonitor(monitor, 0, &dpiX, &dpiY);
    }
    return static_cast<float>(dpiX);
}

}

bool MainWindow::Initialize(HINSTANCE instance, int showCommand)
{
    (void)showCommand;
    instance_ = instance;
    wakeMessage_ = RegisterWindowMessageW(WakeMessageName);
    if (wakeMessage_ == 0)
    {
        DebugLogHResult(L"RegisterWindowMessageW", HResultFromLastError());
        return false;
    }

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = MainWindow::WindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszMenuName = nullptr;
    windowClass.lpszClassName = MainWindow::WindowClassName;
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (RegisterClassExW(&windowClass) == 0)
    {
        DebugLogHResult(L"RegisterClassExW", HResultFromLastError());
        return false;
    }

    constexpr DWORD style = WS_POPUP | WS_CLIPCHILDREN;
    constexpr DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    targetMonitor_ = ResolveTargetMonitor();
    const MONITORINFO monitorInfo = LoadMonitorInfo(targetMonitor_);
    targetMonitorRectPx_ = monitorInfo.rcMonitor;
    targetWorkAreaPx_ = monitorInfo.rcWork;

    const RECT windowRect = targetMonitorRectPx_;

    hwnd_ = CreateWindowExW(
        exStyle,
        MainWindow::WindowClassName,
        kWindowTitle,
        style,
        windowRect.left,
        windowRect.top,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance_,
        this);

    if (!hwnd_)
    {
        DebugLogHResult(L"CreateWindowExW", HResultFromLastError());
        return false;
    }

    ApplyWorkAreaInsets();
    ShowWindow(hwnd_, SW_SHOWNA);
    UpdateWindow(hwnd_);
    touchrev::common::dwm::SetWindowCloaked(hwnd_, true);
    isVisible_ = false;
    return true;
}

void MainWindow::SetConfigVisibilityCallback(std::function<void(bool)> callback)
{
    configVisibilityCallback_ = std::move(callback);
}

int MainWindow::Run()
{
    MSG message{};
    while (true)
    {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
        {
            return result == 0 ? static_cast<int>(message.wParam) : -1;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = static_cast<MainWindow*>(createStruct->lpCreateParams);
        app->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }

    auto* app = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app)
    {
        return app->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == wakeMessage_)
    {
        switch (static_cast<ActivationCommand>(wParam))
        {
        case ActivationCommand::Show:
            if (configVisibilityCallback_)
            {
                configVisibilityCallback_(true);
            }
            return 0;

        case ActivationCommand::Toggle:
            ToggleSwitcher();
            return 0;

        case ActivationCommand::Hide:
            if (configVisibilityCallback_)
            {
                configVisibilityCallback_(false);
            }
            Hide();
            return 0;
        case ActivationCommand::Exit:
            ExitApplication();
            return 0;
        }
    }

    switch (message)
    {
    case WM_ACTIVATE:
    {
        const WORD state = LOWORD(wParam);
        if (state == WA_INACTIVE)
        {
            if (isVisible_ && !isSyncingLayout_)
            {
                Hide();
            }
        }
        else
        {
            ApplyActivationVisualState(true);
        }
        break;
    }

    case WM_WINDOWPOSCHANGING:
    {
        auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
        const int targetWidth = targetMonitorRectPx_.right - targetMonitorRectPx_.left;
        const int targetHeight = targetMonitorRectPx_.bottom - targetMonitorRectPx_.top;
        if (targetWidth > 0 && targetHeight > 0)
        {
            pos->x = targetMonitorRectPx_.left;
            pos->y = targetMonitorRectPx_.top;
            pos->cx = targetWidth;
            pos->cy = targetHeight;
            pos->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        }
        break; // 继续往下分发以允许其它必要处理，但大小坐标已被锁定
    }

    case WM_GETMINMAXINFO:
    {
        const auto minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
        minMaxInfo->ptMaxTrackSize.x = 16384;
        minMaxInfo->ptMaxTrackSize.y = 16384;
        return 0;
    }
    case WM_CREATE:
        return SUCCEEDED(OnCreate()) ? 0 : -1;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            OnSize(ClientWidthFromLParam(lParam), ClientHeightFromLParam(lParam));
        }
        return 0;

    case WM_DPICHANGED:
        OnDpiChanged(wParam, lParam);
        return 0;

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        RefreshTheme();
        return 0;

    case kWmDwmCompositionChanged:
        themeManager_.ApplyWindowBackdrop(hwnd_);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (HandleKeyDown(wParam))
        {
            return 0;
        }
        break;

    case kPrewarmMessage:
        PrewarmSwitcher();
        return 0;

    case kStartWindowDragMessage:
        StartPendingWindowDrag();
        return 0;

    case kPrepareDragProxyMessage:
        dragProxy_.Prepare(instance_);
        return 0;

    case WM_INPUT:
    {
        if (!touchrev::settings::Current().gestureEnabled)
        {
            // 关闭时丢弃半截会话，重新开启后不会带着陈旧触点继续识别。
            if (!gestureInputSuspended_)
            {
                inputController_.ResetGesture();
                gestureInputSuspended_ = true;
                UpdateGestureTimer();
            }
            break;
        }

        gestureInputSuspended_ = false;
        for (const InputController::RawInputResult& result : inputController_.OnRawInput(lParam))
        {
            DispatchGestureAction(result);
        }
        UpdateGestureTimer();
        // RIM_INPUT 需要交给 DefWindowProc 做清理。
        break;
    }

    case WM_INPUT_DEVICE_CHANGE:
        inputController_.OnInputDeviceChange(wParam, lParam);
        UpdateGestureTimer();
        return 0;

    case WM_TIMER:
    {
        if (wParam == kGestureTimerId)
        {
            if (touchrev::settings::Current().gestureEnabled)
            {
                for (const InputController::RawInputResult& result : inputController_.OnGestureTick())
                {
                    DispatchGestureAction(result);
                }
            }
            UpdateGestureTimer();
            return 0;
        }
        if (wParam == kRevealFallbackTimerId)
        {
            RevealNow();
            return 0;
        }
        break;
    }

    case WM_POINTERDOWN:
    {
        const bool canStartDrag = CanStartDragFromPointer(wParam);
        if (!canStartDrag)
        {
            Hide();
            return 0;
        }

        HandleInputResult(inputController_.OnPointerDown(
            hwnd_,
            wParam,
            coordinates_,
            appSwitcherMainView_.DragPosition(),
            canStartDrag));
        return 0;
    }

    case WM_POINTERUPDATE:
        HandleInputResult(inputController_.OnPointerUpdate(hwnd_, wParam, coordinates_));
        return 0;

    case WM_POINTERUP:
        HandleInputResult(inputController_.OnPointerUp(hwnd_, wParam));
        return 0;

    case WM_POINTERCAPTURECHANGED:
        HandleInputResult(inputController_.OnPointerCaptureChanged(wParam));
        return 0;

    case WM_TOUCH:
    {
        const UINT inputCount = LOWORD(wParam);
        std::vector<TOUCHINPUT> inputs(inputCount);
        if (inputCount > 0 &&
            GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lParam), inputCount, inputs.data(), sizeof(TOUCHINPUT)))
        {
            for (const TOUCHINPUT& input : inputs)
            {
                if ((input.dwFlags & TOUCHEVENTF_DOWN) == 0)
                {
                    continue;
                }

                POINT screenPoint{TOUCH_COORD_TO_PIXEL(input.x), TOUCH_COORD_TO_PIXEL(input.y)};
                if (!appSwitcherMainView_.HitTest(coordinates_.ScreenPixelsToDips(hwnd_, screenPoint)))
                {
                    CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
                    Hide();
                    return 0;
                }
            }
        }

        HandleInputResult(inputController_.OnTouch(
            hwnd_,
            wParam,
            lParam,
            coordinates_,
            appSwitcherMainView_.DragPosition(),
            true));
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        const bool canStartDrag = CanStartDragFromMouse(lParam);
        if (!canStartDrag)
        {
            Hide();
            return 0;
        }

        HandleInputResult(inputController_.OnMouseDown(
            hwnd_,
            lParam,
            coordinates_,
            appSwitcherMainView_.DragPosition(),
            canStartDrag));
        return 0;
    }

    case WM_MOUSEMOVE:
        HandleInputResult(inputController_.OnMouseMove(hwnd_, wParam, lParam, coordinates_));
        return 0;

    case WM_LBUTTONUP:
        HandleInputResult(inputController_.OnMouseUp(hwnd_));
        return 0;

    case WM_CANCELMODE:
    case WM_KILLFOCUS:
        inputController_.Cancel(hwnd_);
        return 0;

    case WM_DESTROY:
        OnDestroy();
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        break;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

HRESULT MainWindow::OnCreate()
{
    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
    coordinates_.Update(dpi_, GetClientWidth(hwnd_), GetClientHeight(hwnd_));
    themeManager_.Initialize(hwnd_);
    inputController_.Initialize(hwnd_);

    if (!xamlHost_.Initialize(hwnd_))
    {
        DebugLog(L"XAML AppSwitcher host initialization failed.");
        return E_FAIL;
    }

    appSwitcherMainView_.SetMissedInputCallback([this]() {
        Hide();
    });
    appSwitcherMainView_.SetItemActivatedCallback([this](HWND targetHwnd) {
        touchrev::appswitcher::ActivateWindow(targetHwnd);
        Hide();
    });
    appSwitcherMainView_.SetItemDragReleasedCallback([this](HWND targetHwnd, POINT releasePoint) {
        touchrev::appswitcher::ActivateWindow(targetHwnd, releasePoint, targetWorkAreaPx_);
        Hide();
    });
    appSwitcherMainView_.SetItemDragOutCallback([this](const touchrev::appswitcher::MainView::ItemDragOut& dragOut) {
        return HandOffDragToProxy(dragOut);
    });
    dragProxy_.SetFinishedCallback([this](const touchrev::appswitcher::DragProxyWindow::Result& result) {
        ApplyDragProxyResult(result);
    });
    appSwitcherMainView_.SetItemCloseRequestedCallback([](HWND targetHwnd) {
        return touchrev::appswitcher::RequestCloseWindow(targetHwnd);
    });

    if (!appSwitcherMainView_.Initialize(hwnd_, xamlHost_))
    {
        DebugLog(L"XAML AppSwitcher view initialization failed.");
        return E_FAIL;
    }
    appSwitcherMainView_.ApplyTheme(themeManager_.Mode(), true);

    // 预热放到消息队列里做，不拖慢启动路径。
    PostMessageW(hwnd_, kPrewarmMessage, 0, 0);

    return S_OK;
}

void MainWindow::PrewarmSwitcher()
{
    if (isPrewarmed_ || !hwnd_)
    {
        return;
    }
    isPrewarmed_ = true;

    // 窗口此刻仍处于 cloaked 状态，可以安全地把卡片模板、缩略图槽位全部建好，
    // 让首次呼出与后续呼出走同一条热路径。
    const bool wasSyncing = isSyncingLayout_;
    isSyncingLayout_ = true;
    SyncClientLayout(GetClientWidth(hwnd_), GetClientHeight(hwnd_), true);
    appSwitcherMainView_.EnsureLayoutCommitted();
    appSwitcherMainView_.ResetSelection();
    isSyncingLayout_ = wasSyncing;

    dragProxy_.Prepare(instance_);
}

void MainWindow::OnDestroy()
{
    CancelPendingReveal();
    KillTimer(hwnd_, kGestureTimerId);
    gestureTimerActive_ = false;
    dragProxy_.Destroy();
    ReleaseTargetCloak();
    appSwitcherMainView_.Shutdown();
    xamlHost_.Shutdown();

    HWND configHwnd = FindWindowW(L"TouchRevGUI.ConfigWindow", nullptr);
    if (configHwnd)
    {
        DestroyWindow(configHwnd);
    }

    PostQuitMessage(0);
}

void MainWindow::OnSize(UINT width, UINT height)
{
    if (isSyncingLayout_)
    {
        return;
    }
    SyncClientLayout(width, height, false);
    RebaseActiveDrag();
}

void MainWindow::OnDpiChanged(WPARAM wParam, LPARAM lParam)
{
    const float newDpi = static_cast<float>(LOWORD(wParam));

    // 以窗口实际所在的显示器为准：切换器可能因触摸出现在鼠标之外的另一块屏上。
    HMONITOR currentMonitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    const float realMonitorDpi = GetMonitorDpi(currentMonitor);

    // Filter out transient DPI messages that carry stale scaling factors during monitor transitions.
    if (newDpi != realMonitorDpi)
    {
        return;
    }

    // Skip redundant layout cycles if we have already preset this DPI factor in ShowSwitcher.
    if (newDpi == dpi_)
    {
        ForwardDpiChangeToChild(wParam, lParam);
        return;
    }

    dpi_ = newDpi;
    coordinates_.SetDpi(dpi_);

    ForwardDpiChangeToChild(wParam, lParam);

    const bool wasSyncing = isSyncingLayout_;
    isSyncingLayout_ = true;

    const auto suggestedRect = reinterpret_cast<RECT*>(lParam);
    if (suggestedRect)
    {
        SetWindowPos(
            hwnd_,
            HWND_TOPMOST,
            suggestedRect->left,
            suggestedRect->top,
            suggestedRect->right - suggestedRect->left,
            suggestedRect->bottom - suggestedRect->top,
            SWP_NOACTIVATE);
        const int width = suggestedRect->right - suggestedRect->left;
        const int height = suggestedRect->bottom - suggestedRect->top;
        SyncClientLayout(width, height, false);
    }
    else
    {
        SyncClientLayout(GetClientWidth(hwnd_), GetClientHeight(hwnd_), false);
    }

    isSyncingLayout_ = wasSyncing;
    RebaseActiveDrag();
}

void MainWindow::OnPaint()
{
    PAINTSTRUCT paint{};
    BeginPaint(hwnd_, &paint);
    EndPaint(hwnd_, &paint);
}

void MainWindow::RefreshTheme()
{
    const bool changed = themeManager_.Refresh(hwnd_);
    if (changed)
    {
        appSwitcherMainView_.ApplyTheme(themeManager_.Mode(), activationSucceeded_);
    }
}

void MainWindow::ApplyActivationVisualState(bool active)
{
    activationSucceeded_ = active;
    keyboardNavigationEnabled_ = activationSucceeded_;
    appSwitcherMainView_.ApplyTheme(themeManager_.Mode(), activationSucceeded_);
}

MainWindow::ActivationResult MainWindow::TryActivateSwitcher()
{
    ActivationResult result{};
    result.actualForeground = GetForegroundWindow();
    if (result.actualForeground == hwnd_)
    {
        result.foreground = true;
        result.directSucceeded = true;
        SetActiveWindow(hwnd_);
        SetFocus(hwnd_);
        return result;
    }

    // 第一次尝试：直接调用 SetForegroundWindow
    result.directSucceeded = SetForegroundWindow(hwnd_) != FALSE && GetForegroundWindow() == hwnd_;

    // 回退：如果直接激活失败，通过 AttachThreadInput 借用前台线程的输入权限
    if (!result.directSucceeded)
    {
        const DWORD foregroundThreadId = GetWindowThreadProcessId(result.actualForeground, nullptr);
        const DWORD currentThreadId = GetCurrentThreadId();

        if (foregroundThreadId != 0 && foregroundThreadId != currentThreadId)
        {
            AttachThreadInput(currentThreadId, foregroundThreadId, TRUE);
            BringWindowToTop(hwnd_);
            result.directSucceeded = SetForegroundWindow(hwnd_) != FALSE;
            AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
        }
    }

    if (result.directSucceeded)
    {
        SetActiveWindow(hwnd_);
        SetFocus(hwnd_);
    }

    result.actualForeground = GetForegroundWindow();
    result.foreground = result.actualForeground == hwnd_;

    TR_TRACE(L"[AppSwitcherActivation] foreground=" << result.foreground << L" direct=" << result.directSucceeded
                                                     << L" actualForeground=" << result.actualForeground);
    return result;
}

void MainWindow::SyncClientLayout(UINT width, UINT height, bool renderSwitcher)
{
    coordinates_.Update(dpi_, width, height);
    xamlHost_.Resize(width, height);
    if (renderSwitcher)
    {
        appSwitcherMainView_.RenderSample(width, height, coordinates_.Scale());
    }
    else
    {
        appSwitcherMainView_.Resize(width, height, coordinates_.Scale());
    }
}

void MainWindow::ApplyWorkAreaInsets()
{
    // 自动隐藏的任务栏不占工作区，此时四边内缩均为 0。
    const RECT insets{
        std::max<LONG>(0, targetWorkAreaPx_.left - targetMonitorRectPx_.left),
        std::max<LONG>(0, targetWorkAreaPx_.top - targetMonitorRectPx_.top),
        std::max<LONG>(0, targetMonitorRectPx_.right - targetWorkAreaPx_.right),
        std::max<LONG>(0, targetMonitorRectPx_.bottom - targetWorkAreaPx_.bottom)};
    appSwitcherMainView_.SetWorkAreaInsetsPx(insets);
}

void MainWindow::UpdateGestureTimer()
{
    if (!hwnd_)
    {
        return;
    }

    // 空闲时不留常驻定时器，避免 60Hz 唤醒拖累平板续航；会话期间才需要推进静止长按。
    const bool needed = !gestureInputSuspended_ && inputController_.NeedsGestureTick();
    if (needed && !gestureTimerActive_)
    {
        gestureTimerActive_ = SetTimer(hwnd_, kGestureTimerId, kGestureTimerMs, nullptr) != 0;
    }
    else if (!needed && gestureTimerActive_)
    {
        KillTimer(hwnd_, kGestureTimerId);
        gestureTimerActive_ = false;
    }
}

void MainWindow::RebaseActiveDrag()
{
    inputController_.RebaseActiveDrag(hwnd_, appSwitcherMainView_.DragPosition(), coordinates_);
}

void MainWindow::ForwardDpiChangeToChild(WPARAM wParam, LPARAM lParam)
{
    if (xamlHost_.ChildHwnd())
    {
        SendMessageW(xamlHost_.ChildHwnd(), WM_DPICHANGED, wParam, lParam);
    }
}

void MainWindow::ShowSwitcher(const POINT* touchCenter)
{
    if (!hwnd_)
    {
        return;
    }

    appSwitcherMainView_.ResetSelection();
    CancelPendingReveal();

    isSyncingLayout_ = true;

    targetMonitor_ = ResolveTargetMonitor(touchCenter);
    const MONITORINFO monitorInfo = LoadMonitorInfo(targetMonitor_);
    targetMonitorRectPx_ = monitorInfo.rcMonitor;
    targetWorkAreaPx_ = monitorInfo.rcWork;
    // 每次呼出重新取工作区：任务栏可能被移动、调整大小或切换自动隐藏。
    ApplyWorkAreaInsets();

    dpi_ = GetMonitorDpi(targetMonitor_);
    const RECT windowRect = targetMonitorRectPx_;
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;

    activationSucceeded_ = true;
    keyboardNavigationEnabled_ = true;

    // 主题与窗口几何都要先落定，再跑布局，避免布局之后又被标脏一次。
    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        windowRect.left,
        windowRect.top,
        width,
        height,
        SWP_NOACTIVATE | SWP_FRAMECHANGED);

    RefreshTheme();
    appSwitcherMainView_.ApplyTheme(themeManager_.Mode(), true);

    const WPARAM wp = MAKEWPARAM(static_cast<WORD>(dpi_), static_cast<WORD>(dpi_));
    RECT childSuggestedRect{0, 0, width, height};
    ForwardDpiChangeToChild(wp, reinterpret_cast<LPARAM>(&childSuggestedRect));

    SyncClientLayout(width, height, true);

    // 同步跑完 measure/arrange，然后等合成帧真正提交后再揭示窗口。
    appSwitcherMainView_.EnsureLayoutCommitted();
    BeginRevealAfterFirstFrame();

    isSyncingLayout_ = false;
}

void MainWindow::BeginRevealAfterFirstFrame()
{
    if (revealPending_)
    {
        return;
    }

    revealPending_ = true;
    pendingRevealFrames_ = 0;

    // Rendering 在每帧合成前触发，等满两拍才能确保新布局已经进到已提交的那一帧里。
    renderingToken_ = winrt::Windows::UI::Xaml::Media::CompositionTarget::Rendering(
        [this](auto const&, auto const&) {
            if (++pendingRevealFrames_ < 2)
            {
                return;
            }
            RevealNow();
        });

    // 渲染回调不保证一定到来（XAML 异常、渲染线程阻塞），兜底定时器保证窗口一定会出现。
    SetTimer(hwnd_, kRevealFallbackTimerId, kRevealFallbackMs, nullptr);
}

void MainWindow::RevealNow()
{
    if (!revealPending_ || !hwnd_)
    {
        return;
    }

    CancelPendingReveal();

    touchrev::common::dwm::SetWindowCloaked(hwnd_, false);
    ShowWindow(hwnd_, SW_SHOW);
    isVisible_ = true;

    const bool wasSyncing = isSyncingLayout_;
    isSyncingLayout_ = true;
    const ActivationResult activation = TryActivateSwitcher();
    ApplyActivationVisualState(activation.foreground || activation.directSucceeded);
    isSyncingLayout_ = wasSyncing;
}

void MainWindow::CancelPendingReveal()
{
    if (renderingToken_)
    {
        winrt::Windows::UI::Xaml::Media::CompositionTarget::Rendering(renderingToken_);
        renderingToken_ = {};
    }
    if (hwnd_)
    {
        KillTimer(hwnd_, kRevealFallbackTimerId);
    }
    revealPending_ = false;
    pendingRevealFrames_ = 0;
}

void MainWindow::Hide()
{
    // 揭示还在排队时也要撤销，否则窗口会在隐藏之后又弹出来。
    CancelPendingReveal();

    if (!hwnd_ || !isVisible_)
    {
        return;
    }

    inputController_.Cancel(hwnd_);
    appSwitcherMainView_.CancelInteraction();
    if (!touchrev::common::dwm::SetWindowCloaked(hwnd_, true))
    {
        ShowWindow(hwnd_, SW_HIDE);
    }
    isVisible_ = false;
    isLongPressNavigating_ = false;
    pendingLongPressActivation_ = false;
    activationSucceeded_ = false;
    keyboardNavigationEnabled_ = false;
}

bool MainWindow::HandOffDragToProxy(const touchrev::appswitcher::MainView::ItemDragOut& dragOut)
{
    // 触摸/笔的接触绑定在切换器上，必须先转成鼠标拖拽；转换失败就留在切换器里按原方式拖动。
    if (!dragOut.fromMouse && !touchrev::appswitcher::ConvertPrimaryPointerToMouseDrag())
    {
        return false;
    }

    if (!dragProxy_.Show(instance_, dragOut.hwnd, dragOut.cardRect, dragOut.grabPoint, dragOut.pointer))
    {
        return false;
    }

    // 真实窗口在拖动期间由 shell 隐藏：窗口已被"拎起"，也不能作为另一个窗口出现在 Snap Bar 的组合建议里。
    if (touchrev::appswitcher::SetShellCloak(dragOut.hwnd, true))
    {
        shellCloakedTarget_ = dragOut.hwnd;
    }

    // 切换器是全屏置顶窗口，先隐藏，后续鼠标输入才会落到代理上。
    Hide();
    PostMessageW(hwnd_, kStartWindowDragMessage, 0, 0);
    return true;
}

void MainWindow::StartPendingWindowDrag()
{
    // 交接期间指针可能已经移动，以当前光标位置为起点。
    POINT cursor{};
    GetCursorPos(&cursor);
    if (dragProxy_.StartMove(cursor))
    {
        return;
    }

    const HWND target = dragProxy_.Target();
    dragProxy_.Destroy();
    if (target)
    {
        touchrev::appswitcher::ActivateWindow(target, cursor, targetWorkAreaPx_);
    }
    ReleaseTargetCloak();
}

void MainWindow::ReleaseTargetCloak()
{
    if (const HWND target = std::exchange(shellCloakedTarget_, nullptr))
    {
        touchrev::appswitcher::SetShellCloak(target, false);
    }
}

void MainWindow::ApplyDragProxyResult(const touchrev::appswitcher::DragProxyWindow::Result& result)
{
    // 用过的代理随后被销毁，空闲时预备下一个。
    PostMessageW(hwnd_, kPrepareDragProxyMessage, 0, 0);

    using Kind = touchrev::appswitcher::DragProxyWindow::ResultKind;
    switch (result.kind)
    {
    case Kind::Maximized:
        if (IsIconic(result.target))
        {
            ShowWindow(result.target, SW_RESTORE);
        }
        ShowWindow(result.target, SW_MAXIMIZE);
        SetForegroundWindow(result.target);
        break;
    case Kind::Arranged:
        touchrev::appswitcher::PlaceWindowVisibleRect(result.target, result.arrangedRect);
        break;
    case Kind::Moved:
        touchrev::appswitcher::ActivateWindow(result.target, result.releasePoint, targetWorkAreaPx_);
        break;
    }

    // 摆好位置之后再取消隐藏，窗口直接出现在新位置，不会在原处闪一下。
    ReleaseTargetCloak();
}

void MainWindow::ToggleSwitcher()
{
    if (isVisible_ || revealPending_)
    {
        Hide();
        return;
    }

    ShowSwitcher();
}

void MainWindow::ExitApplication()
{
    if (!hwnd_)
    {
        return;
    }

    isExiting_ = true;
    DestroyWindow(hwnd_);
}

void MainWindow::HandleInputResult(const InputController::Result& result)
{
    if (result.positionChanged || result.dragEnded)
    {
        appSwitcherMainView_.SetDragPosition(result.position);
    }
}

void MainWindow::DispatchGestureAction(const InputController::RawInputResult& inputResult)
{
    if (inputResult.action == InputController::InputAction::ShowSwitcher)
    {
        if (touchrev::settings::Current().switcherWindowEnabled)
        {
            const POINT* touchCenter = inputResult.hasTouchCenter ? &inputResult.touchCenterScreen : nullptr;
            ShowSwitcher(touchCenter);
        }
    }
    else if (inputResult.action == InputController::InputAction::LongPressBegin)
    {
        if (touchrev::settings::Current().switcherWindowEnabled)
        {
            const POINT* touchCenter = inputResult.hasTouchCenter ? &inputResult.touchCenterScreen : nullptr;
            ShowSwitcher(touchCenter);
            isLongPressNavigating_ = true;
            pendingLongPressActivation_ = false;
            appSwitcherMainView_.ClearGestureAccumulator();
        }
    }
    else if (inputResult.action == InputController::InputAction::LongPressMove)
    {
        if (isLongPressNavigating_ && isVisible_)
        {
            appSwitcherMainView_.AccumulateAndMoveSelection(inputResult.deltaX, inputResult.deltaY);
        }
    }
    else if (inputResult.action == InputController::InputAction::LongPressEnd)
    {
        if (isLongPressNavigating_)
        {
            isLongPressNavigating_ = false;
            pendingLongPressActivation_ = isVisible_;
            // 手势结束时未切换的累积量作废，选择框的生长收回。
            appSwitcherMainView_.ClearGestureAccumulator();
        }
    }

    if (pendingLongPressActivation_ && inputResult.allContactsReleased)
    {
        pendingLongPressActivation_ = false;
        if (isVisible_)
        {
            appSwitcherMainView_.ActivateSelectedItem();
        }
    }
}

bool MainWindow::HandleKeyDown(WPARAM key)
{
    if (!keyboardNavigationEnabled_ && key != VK_ESCAPE)
    {
        return false;
    }

    switch (key)
    {
    case VK_ESCAPE:
        Hide();
        return true;

    case VK_RETURN:
    case VK_SPACE:
        return appSwitcherMainView_.ActivateSelectedItem();

    case VK_TAB:
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        {
            return appSwitcherMainView_.MoveSelectionPrevious();
        }
        return appSwitcherMainView_.MoveSelectionNext();

    case VK_LEFT:
        return appSwitcherMainView_.MoveSelection(-1, 0);

    case VK_RIGHT:
        return appSwitcherMainView_.MoveSelection(1, 0);

    case VK_UP:
        return appSwitcherMainView_.MoveSelection(0, -1);

    case VK_DOWN:
        return appSwitcherMainView_.MoveSelection(0, 1);
    }

    return false;
}

HMONITOR MainWindow::ResolveTargetMonitor(const POINT* touchCenter) const
{
    // 优先使用触摸中心坐标（来自三指手势的实际触摸位置）
    if (touchCenter)
    {
        HMONITOR monitor = MonitorFromPoint(*touchCenter, MONITOR_DEFAULTTONEAREST);
        if (monitor)
        {
            TR_TRACE(L"[ResolveTargetMonitor] Using touchCenter=(" << touchCenter->x << L", " << touchCenter->y
                                                                    << L") monitor=" << monitor);
            return monitor;
        }
    }

    // Fallback 到鼠标位置（保持向后兼容，用于非触摸场景）
    POINT cursor{};
    if (GetCursorPos(&cursor))
    {
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        if (monitor)
        {
            TR_TRACE(L"[ResolveTargetMonitor] Using cursor=(" << cursor.x << L", " << cursor.y << L") monitor="
                                                               << monitor);
            return monitor;
        }
    }

    HWND foreground = GetForegroundWindow();
    if (foreground)
    {
        HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
        if (monitor)
        {
            TR_TRACE(L"[ResolveTargetMonitor] Using foreground window monitor");
            return monitor;
        }
    }

    TR_TRACE(L"[ResolveTargetMonitor] Using primary monitor fallback");
    return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

MONITORINFO MainWindow::LoadMonitorInfo(HMONITOR monitor) const
{
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
    {
        monitorInfo.rcMonitor = {0, 0, 1000, 700};
        monitorInfo.rcWork = monitorInfo.rcMonitor;
    }
    return monitorInfo;
}

bool MainWindow::CanStartDragFromPointer(WPARAM wParam) const
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INFO pointerInfo = {};
    if (!GetPointerInfo(pointerId, &pointerInfo))
    {
        return false;
    }

    return appSwitcherMainView_.HitTest(coordinates_.ScreenPixelsToDips(hwnd_, pointerInfo.ptPixelLocation));
}

bool MainWindow::CanStartDragFromMouse(LPARAM lParam) const
{
    POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    return appSwitcherMainView_.HitTest(coordinates_.ClientPixelsToDips(clientPoint));
}

UINT MainWindow::ClientWidthFromLParam(LPARAM lParam)
{
    return std::max<UINT>(1u, static_cast<UINT>(LOWORD(lParam)));
}

UINT MainWindow::ClientHeightFromLParam(LPARAM lParam)
{
    return std::max<UINT>(1u, static_cast<UINT>(HIWORD(lParam)));
}

UINT MainWindow::GetClientWidth(HWND hwnd)
{
    RECT rect{};
    GetClientRect(hwnd, &rect);
    return static_cast<UINT>(std::max<LONG>(1, rect.right - rect.left));
}

UINT MainWindow::GetClientHeight(HWND hwnd)
{
    RECT rect{};
    GetClientRect(hwnd, &rect);
    return static_cast<UINT>(std::max<LONG>(1, rect.bottom - rect.top));
}
