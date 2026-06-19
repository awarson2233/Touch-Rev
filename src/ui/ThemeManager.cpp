#include "ThemeManager.h"

#include "common/Win32Error.h"
#include <dwmapi.h>

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

#if defined(DWMSBT_NONE)
constexpr int kDwmsbtNone = DWMSBT_NONE;
#else
constexpr int kDwmsbtNone = 1;
#endif

#if defined(DWMWA_COLOR_NONE)
constexpr COLORREF kDwmColorNone = DWMWA_COLOR_NONE;
#else
constexpr COLORREF kDwmColorNone = 0xFFFFFFFE;
#endif

winrt::Windows::UI::Color Color(unsigned char a, unsigned char r, unsigned char g, unsigned char b)
{
    return {a, r, g, b};
}
}

void ThemeManager::Initialize(HWND hwnd)
{
    mode_ = ReadSystemTheme();
    palette_ = PaletteForTheme(mode_);
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
    palette_ = PaletteForTheme(mode_);
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

    int backdrop = kDwmsbtNone;
    hr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaSystemBackdropType,
        &backdrop,
        sizeof(backdrop));
    if (FAILED(hr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)", hr);
    }
}

AppThemeMode ThemeManager::ReadSystemTheme()
{
    DWORD value = 0;
    DWORD valueSize = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        kPersonalizeKey,
        kAppsUseLightThemeValue,
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &valueSize);

    if (status != ERROR_SUCCESS)
    {
        return AppThemeMode::Dark;
    }

    return value == 0 ? AppThemeMode::Dark : AppThemeMode::Light;
}

AppSwitcherPalette ThemeManager::PaletteForTheme(AppThemeMode mode)
{
    if (mode == AppThemeMode::Light)
    {
        return {
            Color(0x00, 0x00, 0x00, 0x00),
            Color(0xE8, 0xF3, 0xF3, 0xF3),
            Color(0x33, 0x00, 0x00, 0x00),
            Color(0xFF, 0xFF, 0xFF, 0xFF),
            Color(0xE8, 0xF3, 0xF3, 0xF3),
            0.55,
            Color(0xFF, 0xFF, 0xFF, 0xFF),
            Color(0xFF, 0xF9, 0xF9, 0xF9),
            Color(0xFF, 0xFF, 0xFF, 0xFF),
            Color(0xFF, 0xF0, 0xF0, 0xF0),
            Color(0xFF, 0xE8, 0xE8, 0xE8),
            Color(0xFF, 0xF5, 0xF5, 0xF5),
            Color(0xE8, 0x12, 0x12, 0x12),
            Color(0x99, 0x20, 0x20, 0x20),
            Color(0xCC, 0x20, 0x20, 0x20),
            Color(0xFF, 0x00, 0x00, 0x00),
            Color(0xFF, 0xC4, 0x2B, 0x1C),
            Color(0xFF, 0xFF, 0xFF, 0xFF),
            Color(0xFF, 0x00, 0x78, 0xD4),
            Color(0x22, 0x00, 0x78, 0xD4),
        };
    }

    return {
        Color(0x00, 0x00, 0x00, 0x00),
        Color(0xCC, 0x18, 0x18, 0x18),
        Color(0x55, 0xFF, 0xFF, 0xFF),
        Color(0xFF, 0x20, 0x20, 0x20),
        Color(0xE8, 0x20, 0x20, 0x20),
        0.65,
        Color(0xEE, 0x24, 0x24, 0x24),
        Color(0xFF, 0x27, 0x27, 0x27),
        Color(0xFF, 0x32, 0x32, 0x32),
        Color(0xFF, 0x22, 0x22, 0x22),
        Color(0xFF, 0x1F, 0x1F, 0x1F),
        Color(0xFF, 0x10, 0x10, 0x10),
        Color(0xFF, 0xFF, 0xFF, 0xFF),
        Color(0xCC, 0xFF, 0xFF, 0xFF),
        Color(0xCC, 0xFF, 0xFF, 0xFF),
        Color(0xFF, 0xFF, 0xFF, 0xFF),
        Color(0xFF, 0xC4, 0x2B, 0x1C),
        Color(0xFF, 0xFF, 0xFF, 0xFF),
        Color(0xFF, 0x4C, 0xC2, 0xFF),
        Color(0x22, 0x4C, 0xC2, 0xFF),
    };
}
