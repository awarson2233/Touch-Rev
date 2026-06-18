#include "AppWindow.h"

#include "common/Win32Error.h"

#include <algorithm>
#include <vector>
#include <windowsx.h>

namespace
{
constexpr wchar_t kWindowClassName[] = L"TouchRevGUI.MainWindow";
constexpr wchar_t kWindowTitle[] = L"Touch Rev GUI";

#if defined(WM_DWMCOMPOSITIONCHANGED)
constexpr UINT kWmDwmCompositionChanged = WM_DWMCOMPOSITIONCHANGED;
#else
constexpr UINT kWmDwmCompositionChanged = 0x031E;
#endif
}

bool AppWindow::Initialize(HINSTANCE instance, int showCommand)
{
    instance_ = instance;

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = AppWindow::WindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszMenuName = nullptr;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (RegisterClassExW(&windowClass) == 0)
    {
        DebugLogHResult(L"RegisterClassExW", HResultFromLastError());
        return false;
    }

    constexpr DWORD style = WS_POPUP | WS_CLIPCHILDREN;
    constexpr DWORD exStyle = WS_EX_TOOLWINDOW;
    targetMonitor_ = ResolveTargetMonitor();
    const MONITORINFO monitorInfo = LoadMonitorInfo(targetMonitor_);
    targetMonitorRectPx_ = monitorInfo.rcMonitor;
    targetWorkAreaPx_ = monitorInfo.rcWork;

    const RECT windowRect = targetMonitorRectPx_;

    hwnd_ = CreateWindowExW(
        exStyle,
        kWindowClassName,
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
    UpdateWindow(hwnd_);
    return true;
}

int AppWindow::Run()
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

LRESULT CALLBACK AppWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = static_cast<AppWindow*>(createStruct->lpCreateParams);
        app->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }

    auto* app = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app)
    {
        return app->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT AppWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
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
        ApplyBackdropAndBackgroundMode();
        return 0;

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
            appSwitcherXamlView_.DragPosition(),
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
                if (!appSwitcherXamlView_.HitTest(coordinates_.ScreenPixelsToDips(hwnd_, screenPoint)))
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
            appSwitcherXamlView_.DragPosition(),
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
            appSwitcherXamlView_.DragPosition(),
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

HRESULT AppWindow::OnCreate()
{
    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
    coordinates_.Update(dpi_, GetClientWidth(hwnd_), GetClientHeight(hwnd_));
    themeManager_.Initialize();
    ApplyBackdropAndBackgroundMode();
    inputController_.Initialize(hwnd_);

    if (!xamlHost_.Initialize(hwnd_))
    {
        DebugLog(L"XAML AppSwitcher host initialization failed.");
        return E_FAIL;
    }

    appSwitcherXamlView_.SetTargetMonitor(targetMonitor_);
    appSwitcherXamlView_.SetMissedInputCallback([this]() {
        Hide();
    });
    appSwitcherXamlView_.SetItemActivatedCallback([this](HWND targetHwnd) {
        ActivateWindow(targetHwnd);
        Hide();
    });
    appSwitcherXamlView_.SetItemDragReleasedCallback([this](HWND targetHwnd, POINT releasePoint) {
        ExpandWindowAroundPoint(targetHwnd, releasePoint);
        Hide();
    });

    if (!appSwitcherXamlView_.Initialize(hwnd_, xamlHost_))
    {
        DebugLog(L"XAML AppSwitcher view initialization failed.");
        return E_FAIL;
    }
    appSwitcherXamlView_.ApplyTheme(themeManager_.Palette());
    UpdateTransparentRegion();

    return S_OK;
}

void AppWindow::OnDestroy()
{
    appSwitcherXamlView_.Shutdown();
    xamlHost_.Shutdown();
    PostQuitMessage(0);
}

void AppWindow::OnSize(UINT width, UINT height)
{
    coordinates_.Update(dpi_, width, height);
    xamlHost_.Resize(width, height);
    appSwitcherXamlView_.Resize(width, height, coordinates_.Scale());
    UpdateTransparentRegion();
    inputController_.RebaseActiveDrag(hwnd_, appSwitcherXamlView_.DragPosition(), coordinates_);
}

void AppWindow::OnDpiChanged(WPARAM wParam, LPARAM lParam)
{
    dpi_ = static_cast<float>(LOWORD(wParam));
    coordinates_.SetDpi(dpi_);

    const auto suggestedRect = reinterpret_cast<RECT*>(lParam);
    if (suggestedRect)
    {
        SetWindowPos(
            hwnd_,
            nullptr,
            suggestedRect->left,
            suggestedRect->top,
            suggestedRect->right - suggestedRect->left,
            suggestedRect->bottom - suggestedRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    const UINT clientWidth = GetClientWidth(hwnd_);
    const UINT clientHeight = GetClientHeight(hwnd_);
    coordinates_.SetClientSize(clientWidth, clientHeight);
    xamlHost_.Resize(clientWidth, clientHeight);
    appSwitcherXamlView_.Resize(clientWidth, clientHeight, coordinates_.Scale());
    UpdateTransparentRegion();
    inputController_.RebaseActiveDrag(hwnd_, appSwitcherXamlView_.DragPosition(), coordinates_);
}

void AppWindow::OnPaint()
{
    PAINTSTRUCT paint{};
    BeginPaint(hwnd_, &paint);
    EndPaint(hwnd_, &paint);
}

void AppWindow::ApplyBackdropAndBackgroundMode()
{
    backdropController_.Apply(hwnd_, BackdropController::ShouldUseDarkMode());
}

void AppWindow::RefreshTheme()
{
    const bool changed = themeManager_.Refresh();
    ApplyBackdropAndBackgroundMode();
    if (changed)
    {
        appSwitcherXamlView_.ApplyTheme(themeManager_.Palette());
    }
    UpdateTransparentRegion();
}

void AppWindow::UpdateTransparentRegion()
{
    if (!hwnd_)
    {
        return;
    }

    const UINT clientWidth = GetClientWidth(hwnd_);
    const UINT clientHeight = GetClientHeight(hwnd_);
    xamlHost_.Resize(clientWidth, clientHeight);
    SetWindowRgn(hwnd_, nullptr, TRUE);
}

void AppWindow::Hide()
{
    if (hwnd_)
    {
        DestroyWindow(hwnd_);
    }
}

void AppWindow::ActivateWindow(HWND targetHwnd)
{
    if (IsWindow(targetHwnd))
    {
        SetForegroundWindow(targetHwnd);
    }
}

void AppWindow::ExpandWindowAroundPoint(HWND targetHwnd, POINT centerPoint)
{
    if (!IsWindow(targetHwnd))
    {
        return;
    }

    RECT currentRect{};
    if (!GetWindowRect(targetHwnd, &currentRect))
    {
        ActivateWindow(targetHwnd);
        return;
    }

    int width = std::max<int>(160, currentRect.right - currentRect.left);
    int height = std::max<int>(120, currentRect.bottom - currentRect.top);

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(targetHwnd, &placement))
    {
        const int normalWidth = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
        const int normalHeight = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
        if (normalWidth > 0 && normalHeight > 0)
        {
            width = std::max<int>(160, normalWidth);
            height = std::max<int>(120, normalHeight);
        }

        if (placement.showCmd == SW_SHOWMAXIMIZED || placement.showCmd == SW_SHOWMINIMIZED)
        {
            ShowWindow(targetHwnd, SW_RESTORE);
        }
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    HMONITOR monitor = MonitorFromPoint(centerPoint, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
    {
        monitorInfo.rcWork = targetWorkAreaPx_;
    }

    const RECT workArea = monitorInfo.rcWork;
    const int workWidth = std::max<int>(1, workArea.right - workArea.left);
    const int workHeight = std::max<int>(1, workArea.bottom - workArea.top);
    width = std::min(width, workWidth);
    height = std::min(height, workHeight);

    int left = centerPoint.x - width / 2;
    int top = centerPoint.y - height / 2;
    left = std::clamp<int>(left, static_cast<int>(workArea.left), static_cast<int>(workArea.right) - width);
    top = std::clamp<int>(top, static_cast<int>(workArea.top), static_cast<int>(workArea.bottom) - height);

    SetWindowPos(
        targetHwnd,
        HWND_TOP,
        left,
        top,
        width,
        height,
        SWP_SHOWWINDOW);
    ActivateWindow(targetHwnd);
}

void AppWindow::HandleInputResult(const InputController::Result& result)
{
    if (result.positionChanged || result.dragEnded)
    {
        appSwitcherXamlView_.SetDragPosition(result.position);
    }
}

HMONITOR AppWindow::ResolveTargetMonitor() const
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

MONITORINFO AppWindow::LoadMonitorInfo(HMONITOR monitor) const
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

bool AppWindow::CanStartDragFromPointer(WPARAM wParam) const
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INFO pointerInfo = {};
    if (!GetPointerInfo(pointerId, &pointerInfo))
    {
        return false;
    }

    return appSwitcherXamlView_.HitTest(coordinates_.ScreenPixelsToDips(hwnd_, pointerInfo.ptPixelLocation));
}

bool AppWindow::CanStartDragFromMouse(LPARAM lParam) const
{
    POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    return appSwitcherXamlView_.HitTest(coordinates_.ClientPixelsToDips(clientPoint));
}

UINT AppWindow::ClientWidthFromLParam(LPARAM lParam)
{
    return std::max<UINT>(1u, static_cast<UINT>(LOWORD(lParam)));
}

UINT AppWindow::ClientHeightFromLParam(LPARAM lParam)
{
    return std::max<UINT>(1u, static_cast<UINT>(HIWORD(lParam)));
}

UINT AppWindow::GetClientWidth(HWND hwnd)
{
    RECT rect{};
    GetClientRect(hwnd, &rect);
    return static_cast<UINT>(std::max<LONG>(1, rect.right - rect.left));
}

UINT AppWindow::GetClientHeight(HWND hwnd)
{
    RECT rect{};
    GetClientRect(hwnd, &rect);
    return static_cast<UINT>(std::max<LONG>(1, rect.bottom - rect.top));
}
