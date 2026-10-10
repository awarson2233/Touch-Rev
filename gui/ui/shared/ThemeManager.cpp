#include "ThemeManager.h"

#include "common/Win32Error.h"
#include <dwmapi.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
#if defined(DWMWA_USE_IMMERSIVE_DARK_MODE)
constexpr DWORD kDwmwaUseImmersiveDarkMode = DWMWA_USE_IMMERSIVE_DARK_MODE;
#else
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
#endif

#if defined(DWMWA_WINDOW_CORNER_PREFERENCE)
constexpr DWORD kDwmwaWindowCornerPreference = DWMWA_WINDOW_CORNER_PREFERENCE;
#else
constexpr DWORD kDwmwaWindowCornerPreference = 33;
#endif

#if defined(DWMWA_SYSTEMBACKDROP_TYPE)
constexpr DWORD kDwmwaSystemBackdropType = DWMWA_SYSTEMBACKDROP_TYPE;
#else
constexpr DWORD kDwmwaSystemBackdropType = 38;
#endif

#if defined(DWMWA_USE_HOSTBACKDROPBRUSH)
constexpr DWORD kDwmwaUseHostBackdropBrush = DWMWA_USE_HOSTBACKDROPBRUSH;
#else
constexpr DWORD kDwmwaUseHostBackdropBrush = 17;
#endif

#if defined(DWMWCP_DONOTROUND)
constexpr int kDwmwcpDoNotRound = DWMWCP_DONOTROUND;
#else
constexpr int kDwmwcpDoNotRound = 1;
#endif

#if defined(DWMWCP_ROUND)
constexpr int kDwmwcpRound = DWMWCP_ROUND;
#else
constexpr int kDwmwcpRound = 2;
#endif

#if defined(DWMWA_BORDER_COLOR)
constexpr DWORD kDwmwaBorderColor = DWMWA_BORDER_COLOR;
#else
constexpr DWORD kDwmwaBorderColor = 34;
#endif

#if defined(DWMWA_COLOR_NONE)
constexpr COLORREF kDwmColorNone = DWMWA_COLOR_NONE;
#else
constexpr COLORREF kDwmColorNone = 0xFFFFFFFE;
#endif

#if defined(DWMWA_COLOR_DEFAULT)
constexpr COLORREF kDwmColorDefault = DWMWA_COLOR_DEFAULT;
#else
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;
#endif

// DWM_SYSTEMBACKDROP_TYPE
constexpr DWORD kBackdropNone = 1;        // DWMSBT_NONE
constexpr DWORD kBackdropMainWindow = 2;  // DWMSBT_MAINWINDOW（Mica）

void SetAttribute(HWND hwnd, DWORD attribute, const void* value, DWORD size, PCWSTR name)
{
    const HRESULT hr = DwmSetWindowAttribute(hwnd, attribute, value, size);
    if (FAILED(hr))
    {
        DebugLogHResult(name, hr);
    }
}
}

void ThemeManager::Initialize(HWND hwnd, WindowBackdrop backdrop, bool roundedCorners)
{
    backdrop_ = backdrop;
    roundedCorners_ = roundedCorners;
    mode_ = ReadSystemTheme();
    if (hwnd)
    {
        ApplyWindowBackdrop(hwnd);
    }
}

bool ThemeManager::Refresh(HWND hwnd)
{
    const AppThemeMode newMode = ReadSystemTheme();
    if (hwnd)
    {
        ApplyWindowBackdrop(hwnd);
    }

    if (newMode == mode_)
    {
        return false;
    }

    mode_ = newMode;
    return true;
}

void ThemeManager::ApplyWindowBackdrop(HWND hwnd) const
{
    const BOOL darkMode = (mode_ == AppThemeMode::Dark) ? TRUE : FALSE;
    SetAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &darkMode, sizeof(darkMode),
                 L"DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)");

    const int cornerPreference = roundedCorners_ ? kDwmwcpRound : kDwmwcpDoNotRound;
    SetAttribute(hwnd, kDwmwaWindowCornerPreference, &cornerPreference, sizeof(cornerPreference),
                 L"DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)");

    // Mica 窗口保留系统边框色（暗色下的细描边是观感的一部分），无背板窗口去掉边框。
    const COLORREF borderColor = (backdrop_ == WindowBackdrop::Mica) ? kDwmColorDefault : kDwmColorNone;
    SetAttribute(hwnd, kDwmwaBorderColor, &borderColor, sizeof(borderColor),
                 L"DwmSetWindowAttribute(DWMWA_BORDER_COLOR)");

    const DWORD backdropType = (backdrop_ == WindowBackdrop::Mica) ? kBackdropMainWindow : kBackdropNone;
    SetAttribute(hwnd, kDwmwaSystemBackdropType, &backdropType, sizeof(backdropType),
                 L"DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)");

    if (backdrop_ == WindowBackdrop::Mica)
    {
        // Mica 由 DWM 直接绘制在窗口内容之下，铺满整个窗口（含被 WM_NCCALCSIZE 并入客户区的
        // 标题栏）。此时绝不能再把框架扩展进客户区——那会叠加一层"玻璃片"，把窗口整块透穿成桌面。
        return;
    }

    const BOOL hostBackdropBrush = TRUE;
    SetAttribute(hwnd, kDwmwaUseHostBackdropBrush, &hostBackdropBrush, sizeof(hostBackdropBrush),
                 L"DwmSetWindowAttribute(DWMWA_USE_HOSTBACKDROPBRUSH)");

    const MARGINS margins{-1, -1, -1, -1};
    const HRESULT hr = DwmExtendFrameIntoClientArea(hwnd, &margins);
    if (FAILED(hr))
    {
        DebugLogHResult(L"DwmExtendFrameIntoClientArea", hr);
    }
}

AppThemeMode ThemeManager::ReadSystemTheme()
{
    try
    {
        winrt::Windows::UI::ViewManagement::UISettings settings;
        auto color = settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Background);
        if (color.R > 128 && color.G > 128 && color.B > 128)
        {
            return AppThemeMode::Light;
        }
    }
    catch (...)
    {
    }
    return AppThemeMode::Dark;
}
