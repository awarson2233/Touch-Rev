#include "DragProxyWindow.h"

#include "common/Win32Error.h"

#include <dwmapi.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <iterator>
#include <sstream>

namespace
{
constexpr wchar_t kProxyClassName[] = L"TouchRevGUI.DragProxy";

enum MoveSizeOperation : int
{
    kMsoMove = 9, // MSO_MOVE
};

using EnterMoveSizeLoopFn = BOOL(WINAPI*)(HWND, POINT, MoveSizeOperation);
using IsWindowArrangedFn = BOOL(WINAPI*)(HWND);

template <typename Fn>
Fn User32Proc(const char* name)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? reinterpret_cast<Fn>(GetProcAddress(user32, name)) : nullptr;
}

std::wstring RectText(const RECT& rect)
{
    std::wstringstream text;
    text << L"(" << rect.left << L", " << rect.top << L", " << rect.right << L", " << rect.bottom << L")";
    return text.str();
}

void Log(const std::wstring& message)
{
    DebugLog(L"[DragProxy] " + message);
}

// 移动循环里左键（触摸已转成鼠标拖拽）是否仍按着；按着时的尺寸变化是系统的拖动动画，不是最终排列。
bool IsDragButtonDown()
{
    const int button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    return (GetAsyncKeyState(button) & 0x8000) != 0;
}
}

namespace touchrev::appswitcher
{
void DragProxyWindow::SetFinishedCallback(std::function<void(const Result&)> callback)
{
    finishedCallback_ = std::move(callback);
}

bool DragProxyWindow::Prepare(HINSTANCE instance)
{
    if (hwnd_)
    {
        return true;
    }

    static const bool registered = [instance]() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kProxyClassName;
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered)
    {
        DebugLogHResult(L"[DragProxy] RegisterClassExW", HResultFromLastError());
        return false;
    }

    const ULONGLONG begin = GetTickCount64();

    // 系统只对可调整大小、可最大化的窗口提供贴靠，因此保留完整的重叠窗口样式；
    // 非客户区由 WM_NCCALCSIZE 去掉。没有重定向位面，卡片圆角外由 XAML 的透明像素直接透出桌面。
    hwnd_ = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP,
        kProxyClassName,
        L"",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        instance,
        this);
    if (!hwnd_)
    {
        DebugLogHResult(L"[DragProxy] CreateWindowExW", HResultFromLastError());
        return false;
    }

    // 创建时的首次 WM_NCCALCSIZE 不走"去掉非客户区"的分支，补一次框架变化让客户区铺满窗口。
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    // 关掉 DWM 的窗口边框、阴影和圆角，外观完全由卡片模板决定。
    const DWMNCRENDERINGPOLICY ncPolicy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(hwnd_, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy));

    if (!BuildContent())
    {
        Destroy();
        return false;
    }

    std::wstringstream message;
    message << L"Prepared in " << (GetTickCount64() - begin) << L"ms";
    Log(message.str());
    return true;
}

bool DragProxyWindow::Show(HINSTANCE instance, HWND target, const RECT& cardRect, POINT grabPoint, POINT pointer)
{
    // 上一次的代理没走完流程（理论上不会发生）就丢掉，换一个干净的。
    if (target_)
    {
        Destroy();
    }

    const ULONGLONG begin = GetTickCount64();
    const bool wasPrepared = hwnd_ != nullptr;
    if (!Prepare(instance))
    {
        return false;
    }

    target_ = target;
    arranged_ = false;
    cardSizePx_ = {std::max<LONG>(1, cardRect.right - cardRect.left), std::max<LONG>(1, cardRect.bottom - cardRect.top)};
    grabOffset_ = {grabPoint.x - cardRect.left, grabPoint.y - cardRect.top};
    SetWindowPos(
        hwnd_,
        nullptr,
        pointer.x - grabOffset_.x,
        pointer.y - grabOffset_.y,
        cardSizePx_.cx,
        cardSizePx_.cy,
        SWP_NOZORDER | SWP_NOACTIVATE);

    if (!BindTarget())
    {
        Destroy();
        return false;
    }

    RECT windowRect{};
    RECT frame{};
    GetWindowRect(hwnd_, &windowRect);
    frame = windowRect;
    DwmGetWindowAttribute(hwnd_, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));
    frameInsets_ = {
        frame.left - windowRect.left,
        frame.top - windowRect.top,
        windowRect.right - frame.right,
        windowRect.bottom - frame.bottom};

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);

    std::wstringstream message;
    message << L"Shown target=" << target_ << L" rect=" << RectText(windowRect) << L" prepared=" << wasPrepared
            << L" in " << (GetTickCount64() - begin) << L"ms";
    Log(message.str());
    return true;
}

void DragProxyWindow::MoveGrabPointTo(POINT pointer)
{
    SetWindowPos(
        hwnd_,
        nullptr,
        pointer.x - grabOffset_.x,
        pointer.y - grabOffset_.y,
        0,
        0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool DragProxyWindow::BuildContent()
{
    if (!host_.Initialize(hwnd_))
    {
        return false;
    }

    card_ = CardView::Create(0, {});
    if (!card_)
    {
        Log(L"Failed to create card view.");
        return false;
    }

    try
    {
        using namespace winrt::Windows::UI::Xaml;

        card_->root.HorizontalAlignment(HorizontalAlignment::Left);
        card_->root.VerticalAlignment(VerticalAlignment::Top);
        cardScale_ = Media::ScaleTransform();

        Controls::Grid root;
        root.RenderTransform(cardScale_);
        root.Children().Append(card_->root);
        host_.SetRoot(root);
        thumbnailManager_.EnsureDevice();
        return true;
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"[DragProxy] Build card content", error.code());
        return false;
    }
}

bool DragProxyWindow::BindTarget()
{
    try
    {
        // 卡片已在可视树里，缩略图才能挂到它的宿主元素上。
        const double dpiScale = static_cast<double>(std::max<UINT>(96, GetDpiForWindow(hwnd_))) / 96.0;
        wchar_t title[256]{};
        GetWindowTextW(target_, title, static_cast<int>(std::size(title)));
        card_->UpdateState(
            target_,
            title,
            0,
            0.0,
            0.0,
            cardSizePx_.cx / dpiScale,
            cardSizePx_.cy / dpiScale,
            false,
            thumbnailManager_,
            dpiScale);
        return true;
    }
    catch (const winrt::hresult_error& error)
    {
        DebugLogHResult(L"[DragProxy] Bind target", error.code());
        return false;
    }
}

void DragProxyWindow::OnSize(UINT width, UINT height)
{
    if (target_)
    {
        std::wstringstream text;
        text << L"WM_SIZE " << width << L"x" << height << L" card=" << cardSizePx_.cx << L"x" << cardSizePx_.cy
             << L" buttonDown=" << IsDragButtonDown();
        Log(text.str());
    }

    host_.Resize(width, height);
    if (cardScale_ && cardSizePx_.cx > 0 && cardSizePx_.cy > 0)
    {
        const double scale = std::min(
            static_cast<double>(width) / cardSizePx_.cx, static_cast<double>(height) / cardSizePx_.cy);
        cardScale_.ScaleX(scale);
        cardScale_.ScaleY(scale);
    }
}

bool DragProxyWindow::StartMove(POINT cursor)
{
    if (!hwnd_)
    {
        return false;
    }

    static const auto enterMoveSizeLoop = User32Proc<EnterMoveSizeLoopFn>("EnterMoveSizeLoop");
    if (!enterMoveSizeLoop)
    {
        Log(L"EnterMoveSizeLoop is unavailable on this system.");
        return false;
    }

    // 从显示到这里手指可能又移动了一段，重新对齐，移动循环才会以正确的相对位置跟手。
    MoveGrabPointTo(cursor);

    // 移动循环是同步的：松手、系统应用排列之后才返回，返回时结果已经齐全。
    if (!enterMoveSizeLoop(hwnd_, cursor, kMsoMove))
    {
        DebugLogHResult(L"[DragProxy] EnterMoveSizeLoop", HResultFromLastError());
        return false;
    }

    GetCursorPos(&releasePoint_);
    Finish();
    return true;
}

void DragProxyWindow::Destroy()
{
    card_.reset();
    cardScale_ = nullptr;
    host_.Shutdown();
    if (hwnd_)
    {
        const HWND hwnd = hwnd_;
        hwnd_ = nullptr;
        DestroyWindow(hwnd);
    }
    target_ = nullptr;
}

LRESULT CALLBACK DragProxyWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    DragProxyWindow* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<DragProxyWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<DragProxyWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self && self->hwnd_ == hwnd)
    {
        return self->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT DragProxyWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NCCALCSIZE:
        if (wParam)
        {
            return 0;
        }
        break;

    // 代理没有非客户区，也不让系统在激活切换时补画经典标题栏。
    case WM_NCPAINT:
        return 0;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_ERASEBKGND:
        return 1;

    // 内容全部由 XAML 岛合成，没有重定向位面可画。
    case WM_PAINT:
        ValidateRect(hwnd_, nullptr);
        return 0;

    case WM_WINDOWPOSCHANGING:
        OnWindowPosChanging(*reinterpret_cast<WINDOWPOS*>(lParam));
        break;

    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        break;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void DragProxyWindow::OnWindowPosChanging(WINDOWPOS& pos)
{
    // 拖到 Snap Bar 附近时系统会播放缩小动画，那时左键还按着，放行保持原生效果。
    if ((pos.flags & SWP_NOSIZE) != 0 || IsDragButtonDown())
    {
        return;
    }

    // 松手后的尺寸变化是系统应用排列（或从缩小动画恢复）：记下目标矩形，但不让代理真的变大。
    // 是否真的贴靠由 Finish 里的 IsWindowArranged 判定。
    RECT current{};
    GetWindowRect(hwnd_, &current);
    const LONG left = (pos.flags & SWP_NOMOVE) != 0 ? current.left : pos.x;
    const LONG top = (pos.flags & SWP_NOMOVE) != 0 ? current.top : pos.y;
    arrangedWindowRect_ = {left, top, left + pos.cx, top + pos.cy};
    arranged_ = true;
    pos.flags |= SWP_NOSIZE | SWP_NOMOVE;
}

void DragProxyWindow::Finish()
{
    static const auto isWindowArranged = User32Proc<IsWindowArrangedFn>("IsWindowArranged");

    // 旧系统没有 IsWindowArranged 时只能信截下的矩形。
    const bool systemArranged = isWindowArranged ? isWindowArranged(hwnd_) != FALSE : arranged_;

    Result result;
    result.target = target_;
    result.releasePoint = releasePoint_;
    if (IsZoomed(hwnd_))
    {
        result.kind = ResultKind::Maximized;
    }
    else if (arranged_ && systemArranged)
    {
        result.kind = ResultKind::Arranged;
        result.arrangedRect = {
            arrangedWindowRect_.left + frameInsets_.left,
            arrangedWindowRect_.top + frameInsets_.top,
            arrangedWindowRect_.right - frameInsets_.right,
            arrangedWindowRect_.bottom - frameInsets_.bottom};
    }

    // 先摆好真实窗口再销毁代理，中间不留空档。
    const ULONGLONG begin = GetTickCount64();
    if (finishedCallback_ && result.target)
    {
        finishedCallback_(result);
    }
    const ULONGLONG applied = GetTickCount64();
    Destroy();

    std::wstringstream text;
    text << L"Finish kind=" << static_cast<int>(result.kind) << L" rect=" << RectText(result.arrangedRect)
         << L" systemArranged=" << systemArranged << L" apply=" << (applied - begin) << L"ms destroy="
         << (GetTickCount64() - applied) << L"ms";
    Log(text.str());
}
}
