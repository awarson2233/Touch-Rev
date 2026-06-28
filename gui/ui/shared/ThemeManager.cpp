#include "ThemeManager.h"

#include "common/Win32Error.h"
#include <dwmapi.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
constexpr wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kAppsUseLightThemeValue[] = L"AppsUseLightTheme";

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

}

void ThemeManager::Initialize(HWND hwnd, bool isDialog)
{
    isDialog_ = isDialog;
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
    BOOL darkMode = (mode_ == AppThemeMode::Dark) ? TRUE : FALSE;
    HRESULT hr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaUseImmersiveDarkMode,
        &darkMode,
        sizeof(darkMode));
    if (FAILED(hr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)", hr);
    }

    if (isDialog_)
    {
        // 允许对话框圆角
        int cornerPreference = 2; // DWMWCP_ROUND
        hr = DwmSetWindowAttribute(
            hwnd,
            kDwmwaWindowCornerPreference,
            &cornerPreference,
            sizeof(cornerPreference));
        if (FAILED(hr))
        {
            DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)", hr);
        }
        return;
    }

    int cornerPreference = kDwmwcpDoNotRound;
    hr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaWindowCornerPreference,
        &cornerPreference,
        sizeof(cornerPreference));
    if (FAILED(hr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)", hr);
    }

    COLORREF borderColor = kDwmColorNone;
    hr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaBorderColor,
        &borderColor,
        sizeof(borderColor));
    if (FAILED(hr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_BORDER_COLOR)", hr);
    }

    int backdrop = 1; // DWMSBT_NONE
    hr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaSystemBackdropType,
        &backdrop,
        sizeof(backdrop));
    if (FAILED(hr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)", hr);
    }

    BOOL hostBackdropBrush = TRUE;
    hr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaUseHostBackdropBrush,
        &hostBackdropBrush,
        sizeof(hostBackdropBrush));
    if (FAILED(hr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_USE_HOSTBACKDROPBRUSH)", hr);
    }

    MARGINS margins{-1, -1, -1, -1};
    hr = DwmExtendFrameIntoClientArea(hwnd, &margins);
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


