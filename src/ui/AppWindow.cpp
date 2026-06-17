#include "AppWindow.h"

#include "common/Win32Error.h"

#include <algorithm>
#include <cmath>
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

bool IsDeviceLost(HRESULT hr)
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == D2DERR_RECREATE_TARGET;
}

UINT GetClientWidth(HWND hwnd)
{
    RECT rect{};
    GetClientRect(hwnd, &rect);
    return static_cast<UINT>(std::max<LONG>(1, rect.right - rect.left));
}

UINT GetClientHeight(HWND hwnd)
{
    RECT rect{};
    GetClientRect(hwnd, &rect);
    return static_cast<UINT>(std::max<LONG>(1, rect.bottom - rect.top));
}
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

    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    constexpr DWORD exStyle = 0;
    const UINT initialDpi = GetDpiForSystem();
    RECT windowRect{
        0,
        0,
        MulDiv(1000, static_cast<int>(initialDpi), 96),
        MulDiv(700, static_cast<int>(initialDpi), 96)};
    AdjustWindowRectExForDpi(&windowRect, style, FALSE, exStyle, initialDpi);

    hwnd_ = CreateWindowExW(
        exStyle,
        kWindowClassName,
        kWindowTitle,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
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
    RenderFrames(3);
    return true;
}

int AppWindow::Run()
{
    MSG message{};
    int exitCode = 0;

    while (true)
    {
        if (ShouldDriveRenderFrames())
        {
            HANDLE frameHandle = compositionHost_.FrameLatencyWaitableObject();
            DWORD waitResult = WAIT_FAILED;
            if (frameHandle)
            {
                waitResult = MsgWaitForMultipleObjectsEx(
                    1,
                    &frameHandle,
                    INFINITE,
                    QS_ALLINPUT,
                    MWMO_INPUTAVAILABLE);
            }
            else
            {
                waitResult = MsgWaitForMultipleObjectsEx(
                    0,
                    nullptr,
                    kFallbackRenderFrameMs,
                    QS_ALLINPUT,
                    MWMO_INPUTAVAILABLE);
            }

            const bool frameReady = frameHandle && waitResult == WAIT_OBJECT_0;
            const bool fallbackFrameReady = !frameHandle && waitResult == WAIT_TIMEOUT;
            const bool waitFailed = waitResult == WAIT_FAILED;
            if (waitFailed)
            {
                DebugLogHResult(L"MsgWaitForMultipleObjectsEx", HResultFromLastError());
            }

            if (!DispatchPendingMessages(message, exitCode))
            {
                return exitCode;
            }

            if (frameReady || fallbackFrameReady || waitFailed)
            {
                OnRenderFrame();
            }
            else if (frameHandle && ShouldDriveRenderFrames() && WaitForSingleObject(frameHandle, 0) == WAIT_OBJECT_0)
            {
                OnRenderFrame();
            }
            continue;
        }

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
    case kWmDwmCompositionChanged:
        ApplyBackdropAndBackgroundMode();
        RenderFrames(3);
        return 0;

    case WM_POINTERDOWN:
        HandleInputResult(inputController_.OnPointerDown(hwnd_, wParam, rectangle_, coordinates_));
        return 0;

    case WM_POINTERUPDATE:
        HandleInputResult(inputController_.OnPointerUpdate(hwnd_, wParam, rectangle_, coordinates_));
        return 0;

    case WM_POINTERUP:
        HandleInputResult(inputController_.OnPointerUp(hwnd_, wParam));
        return 0;

    case WM_POINTERCAPTURECHANGED:
        HandleInputResult(inputController_.OnPointerCaptureChanged(wParam));
        return 0;

    case WM_TOUCH:
        HandleInputResult(inputController_.OnTouch(hwnd_, wParam, lParam, rectangle_, coordinates_));
        return 0;

    case WM_LBUTTONDOWN:
        HandleInputResult(inputController_.OnMouseDown(hwnd_, lParam, rectangle_, coordinates_));
        return 0;

    case WM_MOUSEMOVE:
        HandleInputResult(inputController_.OnMouseMove(hwnd_, wParam, lParam, rectangle_, coordinates_));
        return 0;

    case WM_LBUTTONUP:
        HandleInputResult(inputController_.OnMouseUp(hwnd_));
        return 0;

    case WM_TIMER:
        if (wParam == kAnimationTimerId)
        {
            OnAnimationTimer();
            return 0;
        }
        break;

    case WM_CANCELMODE:
    case WM_KILLFOCUS:
        inputController_.Cancel(hwnd_);
        StopAnimationForDirectManipulation();
        RequestRender();
        break;

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

    HRESULT hr = CreateGraphicsResources(true);
    if (FAILED(hr))
    {
        return hr;
    }

    ApplyBackdropAndBackgroundMode();
    RenderFrames(3);
    inputController_.Initialize(hwnd_);
    hr = animationSystem_.Initialize();
    if (FAILED(hr))
    {
        DebugLog(L"Windows Animation Manager is unavailable; drag remains functional without release animation.");
    }

    return S_OK;
}

void AppWindow::OnDestroy()
{
    StopAnimationTimer();
    animationSystem_.Shutdown();
    ReleaseGraphicsResources();
    PostQuitMessage(0);
}

void AppWindow::OnSize(UINT width, UINT height)
{
    ResizeRenderTargets(width, height, true);
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

    renderer_.SetDpi(dpi_);
    ResizeRenderTargets(GetClientWidth(hwnd_), GetClientHeight(hwnd_), true);
}

void AppWindow::ResizeRenderTargets(UINT width, UINT height, bool render)
{
    if (!graphicsReady_ || width == 0 || height == 0)
    {
        return;
    }

    coordinates_.Update(dpi_, width, height);
    PaintBackdropBase();
    renderer_.ReleaseTarget();
    HRESULT hr = compositionHost_.Resize(width, height);
    if (FAILED(hr))
    {
        HandleRenderFailure(hr);
        return;
    }

    hr = renderer_.Resize(dpi_);
    if (FAILED(hr))
    {
        HandleRenderFailure(hr);
        return;
    }

    const SizeDip clientSize = CurrentClientSizeDips();
    rectangle_.SetClientSize(clientSize.width, clientSize.height);
    inputController_.RebaseActiveDrag(hwnd_, rectangle_, coordinates_);

    PaintBackdropBase();
    if (render)
    {
        RenderFrames(3);
    }
}

void AppWindow::OnPaint()
{
    PAINTSTRUCT paint{};
    BeginPaint(hwnd_, &paint);
    FillRect(paint.hdc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    EndPaint(hwnd_, &paint);
    Render();
}

void AppWindow::OnAnimationTimer()
{
    float x = 0.0f;
    float y = 0.0f;
    if (animationSystem_.Update(x, y))
    {
        rectangle_.MoveTo({x, y});
        Render();
    }

    if (!animationSystem_.IsActive())
    {
        StopAnimationTimer();
    }
}

void AppWindow::ApplyBackdropAndBackgroundMode()
{
    const bool useDarkMode = BackdropController::ShouldUseDarkMode();
    const BackdropApplyResult result = backdropController_.Apply(hwnd_, useDarkMode);
    const D2D1_COLOR_F fallbackColor = useDarkMode
                                           ? D2D1_COLOR_F{0.12f, 0.12f, 0.12f, 1.0f}
                                           : D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f};

    renderer_.SetFallbackBackgroundColor(fallbackColor);
    renderer_.SetBackgroundMode(result.systemBackdropApplied
                                    ? Renderer::BackgroundMode::TransparentForMica
                                    : Renderer::BackgroundMode::SolidWhiteFallback);
    PaintBackdropBase();
}

void AppWindow::PaintBackdropBase()
{
    if (!hwnd_)
    {
        return;
    }

    HDC dc = GetDC(hwnd_);
    if (!dc)
    {
        return;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    ReleaseDC(hwnd_, dc);
}

HRESULT AppWindow::CreateGraphicsResources(bool initializeRectangle)
{
    graphicsReady_ = false;
    renderer_.Reset();
    compositionHost_.Reset();
    graphicsDevice_.Reset();

    HRESULT hr = graphicsDevice_.Initialize();
    if (FAILED(hr))
    {
        return hr;
    }

    const UINT width = GetClientWidth(hwnd_);
    const UINT height = GetClientHeight(hwnd_);
    coordinates_.Update(dpi_, width, height);
    hr = compositionHost_.Initialize(hwnd_, graphicsDevice_, width, height);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = renderer_.Initialize(graphicsDevice_, compositionHost_, dpi_);
    if (FAILED(hr))
    {
        return hr;
    }

    const SizeDip clientSize = CurrentClientSizeDips();
    if (initializeRectangle || !hasInitializedRectangle_)
    {
        rectangle_.Initialize(clientSize.width, clientSize.height);
        hasInitializedRectangle_ = true;
    }
    else
    {
        rectangle_.SetClientSize(clientSize.width, clientSize.height);
    }

    graphicsReady_ = true;
    return S_OK;
}

void AppWindow::ReleaseGraphicsResources()
{
    graphicsReady_ = false;
    renderRequested_ = false;
    renderer_.Reset();
    compositionHost_.Reset();
    graphicsDevice_.Reset();
}

void AppWindow::Render()
{
    if (!graphicsReady_)
    {
        return;
    }

    const bool useVisualPosition = inputController_.IsDragging();
    const PointDip visualPosition = useVisualPosition
                                        ? inputController_.EvaluateVisualPosition(rectangle_)
                                        : PointDip{};
    const HRESULT hr = renderer_.Render(rectangle_, useVisualPosition ? &visualPosition : nullptr);
    if (FAILED(hr))
    {
        HandleRenderFailure(hr);
    }
}

void AppWindow::RenderFrames(UINT frameCount)
{
    for (UINT i = 0; i < frameCount; ++i)
    {
        Render();
    }
    renderRequested_ = false;
}

void AppWindow::RequestRender()
{
    if (graphicsReady_)
    {
        renderRequested_ = true;
    }
}

void AppWindow::OnRenderFrame()
{
    if (!graphicsReady_)
    {
        renderRequested_ = false;
        return;
    }

    if (!inputController_.IsDragging() && !renderRequested_)
    {
        return;
    }

    renderRequested_ = false;
    Render();
}

bool AppWindow::ShouldDriveRenderFrames() const
{
    return graphicsReady_ && (renderRequested_ || inputController_.IsDragging());
}

bool AppWindow::DispatchPendingMessages(MSG& message, int& exitCode)
{
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            exitCode = static_cast<int>(message.wParam);
            return false;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return true;
}

void AppWindow::HandleRenderFailure(HRESULT hr)
{
    if (IsDeviceLost(hr))
    {
        DebugLogHResult(L"Render path lost device; recreating graphics resources", hr);
        if (SUCCEEDED(CreateGraphicsResources(false)))
        {
            renderer_.Render(rectangle_);
        }
        return;
    }

    DebugLogHResult(L"Render path", hr);
}

void AppWindow::HandleInputResult(const InputController::Result& result)
{
    if (result.dragStarted)
    {
        StopAnimationForDirectManipulation();
        RequestRender();
    }

    if (result.rectangleChanged)
    {
        RequestRender();
    }

    if (result.dragEnded)
    {
        RequestRender();
        StartReleaseAnimation();
    }
}

void AppWindow::StopAnimationForDirectManipulation()
{
    animationSystem_.Stop();
    StopAnimationTimer();
}

void AppWindow::StartReleaseAnimation()
{
    const PointDip current = rectangle_.Position();
    const PointDip target = rectangle_.ClampPosition(current);
    if (std::abs(current.x - target.x) < 0.001f && std::abs(current.y - target.y) < 0.001f)
    {
        return;
    }

    const HRESULT hr = animationSystem_.Start(current.x, current.y, target.x, target.y);
    if (hr == S_OK)
    {
        StartAnimationTimer();
    }
    else if (FAILED(hr))
    {
        rectangle_.MoveTo(target);
        Render();
    }
}

void AppWindow::StartAnimationTimer()
{
    if (!animationTimerActive_)
    {
        SetTimer(hwnd_, kAnimationTimerId, kAnimationTimerMs, nullptr);
        animationTimerActive_ = true;
    }
}

void AppWindow::StopAnimationTimer()
{
    if (animationTimerActive_ && hwnd_)
    {
        KillTimer(hwnd_, kAnimationTimerId);
    }
    animationTimerActive_ = false;
}

SizeDip AppWindow::CurrentClientSizeDips() const
{
    return coordinates_.ClientSizeDips();
}

UINT AppWindow::ClientWidthFromLParam(LPARAM lParam)
{
    return std::max<UINT>(1u, static_cast<UINT>(LOWORD(lParam)));
}

UINT AppWindow::ClientHeightFromLParam(LPARAM lParam)
{
    return std::max<UINT>(1u, static_cast<UINT>(HIWORD(lParam)));
}
