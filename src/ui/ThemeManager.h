#pragma once

#include "AppTheme.h"

class ThemeManager
{
public:
    void Initialize();
    bool Refresh();

    AppThemeMode Mode() const { return mode_; }
    const AppSwitcherPalette& Palette() const { return palette_; }

private:
    static AppThemeMode ReadSystemTheme();

    AppThemeMode mode_ = AppThemeMode::Dark;
    AppSwitcherPalette palette_ = PaletteForTheme(AppThemeMode::Dark);
};
