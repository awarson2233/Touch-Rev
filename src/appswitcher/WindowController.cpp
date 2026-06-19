#include "WindowController.h"

#include "common/Win32Error.h"

#include <dwmapi.h>

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

    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
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

    SetForegroundWindow(targetHwnd);
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
}
