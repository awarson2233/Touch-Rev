#include "MainWindow.h"

#include "appswitcher/WindowController.h"
#include "common/Win32Error.h"
#include "common/AppSettings.h"

#include <algorithm>
#include <vector>
#include <windowsx.h>

namespace
{
constexpr wchar_t kWindowTitle[] = L"Touch Rev GUI";

// 与 RawTouchInputViewer 对齐：定时推进三指长按状态机（静止时硬件不持续上报帧）。
constexpr UINT_PTR kGestureTimerId = 1;
constexpr UINT kGestureTimerMs = 16;

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

    ShowWindow(hwnd_, showCommand);
    isVisible_ = showCommand != SW_HIDE;
    UpdateWindow(hwnd_);
    return true;
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
        case ActivationCommand::Toggle:
        {
            HWND configHwnd = FindWindowW(L"TouchRevGUI.ConfigWindow", nullptr);
            if (configHwnd)
            {
                ShowWindow(configHwnd, SW_SHOW);
                SetForegroundWindow(configHwnd);
            }
            return 0;
        }
        case ActivationCommand::Hide:
        {
            HWND configHwnd = FindWindowW(L"TouchRevGUI.ConfigWindow", nullptr);
            if (configHwnd)
            {
                ShowWindow(configHwnd, SW_HIDE);
            }
            Hide();
            return 0;
        }
        case ActivationCommand::Exit:
            ExitApplication();
            return 0;
        }
    }

    switch (message)
    {
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

    case WM_INPUT:
    {
        if (!touchrev::settings::g_IsGestureEnabled)
        {
            return 0;
        }

        DispatchGestureAction(inputController_.OnRawInput(lParam));
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == kGestureTimerId)
        {
            if (touchrev::settings::g_IsGestureEnabled)
            {
                DispatchGestureAction(inputController_.OnGestureTick());
            }
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
    appSwitcherMainView_.SetItemCloseRequestedCallback([](HWND targetHwnd) {
        return touchrev::appswitcher::RequestCloseWindow(targetHwnd);
    });

    if (!appSwitcherMainView_.Initialize(hwnd_, xamlHost_))
    {
        DebugLog(L"XAML AppSwitcher view initialization failed.");
        return E_FAIL;
    }
    appSwitcherMainView_.ApplyTheme(themeManager_.Palette());
 
    SetTimer(hwnd_, kGestureTimerId, kGestureTimerMs, nullptr);

    return S_OK;
}

void MainWindow::OnDestroy()
{
    KillTimer(hwnd_, kGestureTimerId);
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
    
    HMONITOR currentMonitor = ResolveTargetMonitor();
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
        appSwitcherMainView_.ApplyTheme(themeManager_.Palette());
    }
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

void MainWindow::ShowSwitcher()
{
    if (!hwnd_)
    {
        return;
    }

    appSwitcherMainView_.ResetSelection();

    isSyncingLayout_ = true;

    targetMonitor_ = ResolveTargetMonitor();
    const MONITORINFO monitorInfo = LoadMonitorInfo(targetMonitor_);
    targetMonitorRectPx_ = monitorInfo.rcMonitor;
    targetWorkAreaPx_ = monitorInfo.rcWork;

    dpi_ = GetMonitorDpi(targetMonitor_);
    const RECT windowRect = targetMonitorRectPx_;
    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        windowRect.left,
        windowRect.top,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        SWP_NOACTIVATE);

    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;

    const WPARAM wp = MAKEWPARAM(static_cast<WORD>(dpi_), static_cast<WORD>(dpi_));
    RECT childSuggestedRect{0, 0, width, height};
    ForwardDpiChangeToChild(wp, reinterpret_cast<LPARAM>(&childSuggestedRect));

    SyncClientLayout(width, height, true);
    RefreshTheme();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    isVisible_ = true;

    isSyncingLayout_ = false;
}

void MainWindow::Hide()
{
    if (!hwnd_ || !isVisible_)
    {
        return;
    }

    inputController_.Cancel(hwnd_);
    appSwitcherMainView_.CancelInteraction();
    ShowWindow(hwnd_, SW_HIDE);
    isVisible_ = false;
    isLongPressNavigating_ = false;
    pendingLongPressActivation_ = false;
}

void MainWindow::ToggleSwitcher()
{
    if (isVisible_)
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
        if (touchrev::settings::g_IsSwitcherWindowEnabled)
        {
            ShowSwitcher();
        }
    }
    else if (inputResult.action == InputController::InputAction::LongPressBegin)
    {
        if (touchrev::settings::g_IsSwitcherWindowEnabled)
        {
            ShowSwitcher();
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

HMONITOR MainWindow::ResolveTargetMonitor() const
{
    POINT cursor{};
    if (GetCursorPos(&cursor))
    {
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        if (monitor)
        {
            return monitor;
        }
    }

    HWND foreground = GetForegroundWindow();
    if (foreground)
    {
        HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
        if (monitor)
        {
            return monitor;
        }
    }

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
