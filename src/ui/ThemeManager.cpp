#include "ThemeManager.h"

#include <windows.h>

namespace
{
constexpr wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kAppsUseLightThemeValue[] = L"AppsUseLightTheme";
}

void ThemeManager::Initialize()
{
    mode_ = ReadSystemTheme();
    palette_ = PaletteForTheme(mode_);
}

bool ThemeManager::Refresh()
{
    const AppThemeMode newMode = ReadSystemTheme();
    if (newMode == mode_)
    {
        return false;
    }

    mode_ = newMode;
    palette_ = PaletteForTheme(mode_);
    return true;
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
