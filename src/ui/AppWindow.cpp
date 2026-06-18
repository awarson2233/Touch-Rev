#include "AppWindow.h"

#include "common/Win32Error.h"

#include <algorithm>
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
    constexpr DWORD exStyle = 0;
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
    {
        monitorInfo.rcMonitor = {0, 0, 1000, 700};
    }

    const RECT windowRect = monitorInfo.rcMonitor;

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
        HandleInputResult(inputController_.OnPointerDown(
            hwnd_,
            wParam,
            coordinates_,
            appSwitcherXamlView_.DragPosition(),
            CanStartDragFromPointer(wParam)));
        return 0;

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
        HandleInputResult(inputController_.OnTouch(
            hwnd_,
            wParam,
            lParam,
            coordinates_,
            appSwitcherXamlView_.DragPosition(),
            true));
        return 0;

    case WM_LBUTTONDOWN:
        HandleInputResult(inputController_.OnMouseDown(
            hwnd_,
            lParam,
            coordinates_,
            appSwitcherXamlView_.DragPosition(),
            CanStartDragFromMouse(lParam)));
        return 0;

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

    appSwitcherXamlView_.SetBoundsChangedCallback([this]() {
        UpdateTransparentRegion();
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

    RECT visible = appSwitcherXamlView_.VisibleBoundsPx();
    const int visibleWidth = std::max<int>(1, static_cast<int>(visible.right - visible.left));
    const int visibleHeight = std::max<int>(1, static_cast<int>(visible.bottom - visible.top));
    xamlHost_.SetBounds(
        visible.left,
        visible.top,
        static_cast<UINT>(visibleWidth),
        static_cast<UINT>(visibleHeight));

    POINT clientOrigin{0, 0};
    ClientToScreen(hwnd_, &clientOrigin);

    RECT windowRect{};
    GetWindowRect(hwnd_, &windowRect);
    const int offsetX = clientOrigin.x - windowRect.left;
    const int offsetY = clientOrigin.y - windowRect.top;

    constexpr int bleedPx = 2;
    HRGN region = CreateRectRgn(
        visible.left + offsetX - bleedPx,
        visible.top + offsetY - bleedPx,
        visible.right + offsetX + bleedPx,
        visible.bottom + offsetY + bleedPx);
    if (region == nullptr)
    {
        DebugLogHResult(L"CreateRectRgn", HResultFromLastError());
        return;
    }

    if (SetWindowRgn(hwnd_, region, TRUE) == 0)
    {
        DebugLogHResult(L"SetWindowRgn", HResultFromLastError());
        DeleteObject(region);
    }
}

void AppWindow::HandleInputResult(const InputController::Result& result)
{
    if (result.positionChanged || result.dragEnded)
    {
        appSwitcherXamlView_.SetDragPosition(result.position);
    }
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
