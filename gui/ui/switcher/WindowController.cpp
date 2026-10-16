#include "WindowController.h"

#include "common/Win32Error.h"
#include "common/DwmUtils.h"

#include <dwmapi.h>
#include <inspectable.h>
#include <servprov.h>
#include <winrt/base.h>

#include <algorithm>
#include <iterator>
#include <sstream>

namespace
{
std::wstring GetWindowDisplayTitle(HWND hwnd)
{
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    if (title[0] != L'\0')
    {
        return title;
    }

    wchar_t className[128]{};
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    if (className[0] != L'\0')
    {
        std::wstringstream fallback;
        fallback << className << L" " << hwnd;
        return fallback.str();
    }

    std::wstringstream fallback;
    fallback << L"Window " << hwnd;
    return fallback.str();
}

bool TryGetSwitcherWindowRect(HWND hwnd, RECT& rect)
{
    if (IsIconic(hwnd))
    {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(hwnd, &placement))
        {
            rect = placement.rcNormalPosition;
            return rect.right > rect.left && rect.bottom > rect.top;
        }
    }

    return GetWindowRect(hwnd, &rect) && rect.right > rect.left && rect.bottom > rect.top;
}

bool IsAltTabLikeWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd))
    {
        return false;
    }

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0)
    {
        return false;
    }

    if (touchrev::common::dwm::IsWindowCloaked(hwnd))
    {
        return false;
    }

    RECT rect{};
    if (!TryGetSwitcherWindowRect(hwnd, rect))
    {
        return false;
    }

    return rect.right - rect.left >= 80 && rect.bottom - rect.top >= 60;
}

// Windows 11 24H2 起的 SDK 才有声明，这里按名称动态获取，旧系统上回退。
using ConvertPrimaryPointerToMouseDragFn = BOOL(WINAPI*)();

template <typename Fn>
Fn User32Proc(const char* name)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? reinterpret_cast<Fn>(GetProcAddress(user32, name)) : nullptr;
}

// explorer 的应用视图接口（未公开）。IID 取自注册表 HKCR\Interface 登记的 proxy/stub，方法顺序与
// SetCloak(APPLICATION_VIEW_CLOAK_TYPE, int) 签名取自 twinui.pcshell 符号，build 28120 实测；只声明用到的前缀。
MIDL_INTERFACE("372E1D3B-38D3-42E4-A15B-8AB2B178F513")
IApplicationView : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE SetFocus() = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchTo() = 0;
    virtual HRESULT STDMETHODCALLTYPE TryInvokeBack(IUnknown* callback) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetThumbnailWindow(HWND* hwnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMonitor(IUnknown** monitor) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVisibility(int* visibility) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCloak(int cloakType, int cloaked) = 0;
};

MIDL_INTERFACE("1841C6D7-4F9D-42C0-AF41-8747538F10E5")
IApplicationViewCollection : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetViews(IUnknown** views) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewsByZOrder(IUnknown** views) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewsByAppUserModelId(PCWSTR appUserModelId, IUnknown** views) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewForHwnd(HWND hwnd, IApplicationView** view) = 0;
};

constexpr CLSID kClsidImmersiveShell = {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
// APPLICATION_VIEW_CLOAK_TYPE：1 为默认（DWM 上表现为 DWM_CLOAKED_SHELL）。
constexpr int kApplicationViewCloakDefault = 1;

winrt::com_ptr<IApplicationViewCollection> ConnectViewCollection()
{
    winrt::com_ptr<IServiceProvider> shell;
    HRESULT hr = CoCreateInstance(kClsidImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(shell.put()));
    if (FAILED(hr))
    {
        DebugLogHResult(L"CoCreateInstance(ImmersiveShell)", hr);
        return nullptr;
    }

    winrt::com_ptr<IApplicationViewCollection> collection;
    hr = shell->QueryService(
        __uuidof(IApplicationViewCollection), __uuidof(IApplicationViewCollection), collection.put_void());
    if (FAILED(hr))
    {
        DebugLogHResult(L"QueryService(IApplicationViewCollection)", hr);
        return nullptr;
    }
    return collection;
}
}

namespace touchrev::appswitcher
{
std::vector<WindowItem> EnumerateActiveWindows(HWND excludeHwnd)
{
    struct EnumState
    {
        HWND exclude = nullptr;
        std::vector<WindowItem> windows;
    } state{excludeHwnd};

    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto& state = *reinterpret_cast<EnumState*>(param);
            if (hwnd == state.exclude || !IsAltTabLikeWindow(hwnd))
            {
                return TRUE;
            }

            RECT rect{};
            if (!TryGetSwitcherWindowRect(hwnd, rect))
            {
                return TRUE;
            }

            state.windows.push_back({
                hwnd,
                static_cast<double>(std::max<LONG>(1, rect.right - rect.left)),
                static_cast<double>(std::max<LONG>(1, rect.bottom - rect.top)),
                GetWindowDisplayTitle(hwnd)});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));

    return state.windows;
}

void AcquireForegroundRights()
{
    // 前台锁只放行前台进程或最近收到输入的进程。注入一个空鼠标输入（不移动、不按键），
    // 让本进程成为最近收到输入的进程，之后的 SetForegroundWindow 才不会被拒绝。
    INPUT input{};
    input.type = INPUT_MOUSE;
    SendInput(1, &input, sizeof(input));
}

void ActivateWindow(HWND targetHwnd)
{
    if (!IsWindow(targetHwnd))
    {
        return;
    }

    if (IsIconic(targetHwnd))
    {
        ShowWindow(targetHwnd, SW_RESTORE);
    }

    // 前台切换受前台锁限制：只有前台进程或最近收到输入的进程才能 SetForegroundWindow。
    // 三指长按时触点属于原前台应用，本进程只经 raw input 旁听，直接调用可能被拒绝，需要逐级回退。
    const auto isForeground = [targetHwnd]() {
        const HWND foreground = GetForegroundWindow();
        return foreground == targetHwnd ||
               (foreground && GetAncestor(foreground, GA_ROOTOWNER) == GetAncestor(targetHwnd, GA_ROOTOWNER));
    };

    const HWND previousForeground = GetForegroundWindow();
    if (SetForegroundWindow(targetHwnd) && isForeground())
    {
        TR_TRACE(L"[Activate] direct target=" << targetHwnd << L" previous=" << previousForeground);
        return;
    }

    // 回退一：挂到当前前台线程的输入状态上，借用它的前台权限。
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThreadId = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD currentThreadId = GetCurrentThreadId();
    if (foregroundThreadId != 0 && foregroundThreadId != currentThreadId &&
        AttachThreadInput(currentThreadId, foregroundThreadId, TRUE))
    {
        BringWindowToTop(targetHwnd);
        SetForegroundWindow(targetHwnd);
        AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
        if (isForeground())
        {
            TR_TRACE(L"[Activate] attached target=" << targetHwnd << L" previous=" << previousForeground);
            return;
        }
    }

    // 回退二：切换器显示时已经拿过一次前台权限，长按期间触点仍在别的进程上移动，权限可能已被收回，再拿一次。
    AcquireForegroundRights();
    SetForegroundWindow(targetHwnd);
    TR_TRACE(L"[Activate] injected target=" << targetHwnd << L" previous=" << previousForeground
                                            << L" foreground=" << GetForegroundWindow()
                                            << L" ok=" << isForeground());
}

bool RequestCloseWindow(HWND targetHwnd)
{
    if (!IsWindow(targetHwnd))
    {
        return false;
    }

    if (PostMessageW(targetHwnd, WM_CLOSE, 0, 0))
    {
        return true;
    }

    DebugLogHResult(L"PostMessageW(WM_CLOSE)", HResultFromLastError());
    return false;
}

void ActivateWindow(HWND targetHwnd, POINT centerPoint, const RECT& fallbackWorkAreaPx)
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
        monitorInfo.rcWork = fallbackWorkAreaPx;
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

bool ConvertPrimaryPointerToMouseDrag()
{
    static const auto convert = User32Proc<ConvertPrimaryPointerToMouseDragFn>("ConvertPrimaryPointerToMouseDrag");
    if (!convert)
    {
        TR_TRACE(L"ConvertPrimaryPointerToMouseDrag is unavailable on this system.");
        return false;
    }
    if (!convert())
    {
        DebugLogHResult(L"[WindowDrag] ConvertPrimaryPointerToMouseDrag", HResultFromLastError());
        return false;
    }
    return true;
}

bool SetShellCloak(HWND targetHwnd, bool cloaked)
{
    // 连接跨调用复用；explorer 重启后旧连接失效，失败时重连一次。
    static winrt::com_ptr<IApplicationViewCollection> collection;
    HRESULT hr = E_FAIL;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (!collection)
        {
            collection = ConnectViewCollection();
            if (!collection)
            {
                return false;
            }
        }

        winrt::com_ptr<IApplicationView> view;
        hr = collection->GetViewForHwnd(targetHwnd, view.put());
        if (SUCCEEDED(hr))
        {
            hr = view->SetCloak(kApplicationViewCloakDefault, cloaked ? 1 : 0);
        }
        if (SUCCEEDED(hr))
        {
            return true;
        }
        collection = nullptr;
    }

    DebugLogHResult(cloaked ? L"[ShellCloak] Cloak" : L"[ShellCloak] Uncloak", hr);
    return false;
}

void PlaceWindowVisibleRect(HWND targetHwnd, const RECT& visibleRect)
{
    if (!IsWindow(targetHwnd))
    {
        return;
    }

    if (IsIconic(targetHwnd) || IsZoomed(targetHwnd))
    {
        ShowWindow(targetHwnd, SW_RESTORE);
    }

    RECT windowRect{};
    if (!GetWindowRect(targetHwnd, &windowRect))
    {
        ActivateWindow(targetHwnd);
        return;
    }

    // 窗口矩形含不可见的缩放边框，按 DWM 的可见边界换算，可见部分才正好落在目标矩形上。
    RECT frame = windowRect;
    DwmGetWindowAttribute(targetHwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));
    const int left = visibleRect.left - (frame.left - windowRect.left);
    const int top = visibleRect.top - (frame.top - windowRect.top);
    const int right = visibleRect.right + (windowRect.right - frame.right);
    const int bottom = visibleRect.bottom + (windowRect.bottom - frame.bottom);

    const BOOL placed = SetWindowPos(targetHwnd, HWND_TOP, left, top, right - left, bottom - top, SWP_SHOWWINDOW);
    if (!placed)
    {
        DebugLogHResult(L"[DragProxy] SetWindowPos(target)", HResultFromLastError());
    }
    ActivateWindow(targetHwnd);
}
}
