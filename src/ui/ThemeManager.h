#pragma once

#include <windows.h>
#include <winrt/Windows.UI.h>

enum class AppThemeMode
{
    Light,
    Dark,
};

struct AppSwitcherPalette
{
    winrt::Windows::UI::Color rootBackdrop{};
    winrt::Windows::UI::Color containerBackground{};
    winrt::Windows::UI::Color containerBorder{};
    winrt::Windows::UI::Color containerAcrylicTint{};
    winrt::Windows::UI::Color containerAcrylicFallback{};
    double containerAcrylicTintOpacity = 0.0;
    winrt::Windows::UI::Color cardBackground{};
    winrt::Windows::UI::Color titleBackground{};
    winrt::Windows::UI::Color titleHoverBackground{};
    winrt::Windows::UI::Color titlePressedBackground{};
    winrt::Windows::UI::Color titleGrabbedBackground{};
    winrt::Windows::UI::Color contentBackground{};
    winrt::Windows::UI::Color primaryText{};
    winrt::Windows::UI::Color secondaryText{};
    winrt::Windows::UI::Color iconText{};
    winrt::Windows::UI::Color buttonText{};
    winrt::Windows::UI::Color closeButtonHoverBackground{};
    winrt::Windows::UI::Color closeButtonHoverText{};
    winrt::Windows::UI::Color focusBorder{};
    winrt::Windows::UI::Color focusFill{};
    winrt::Windows::UI::Color cardGrabbedOverlay{};
    winrt::Windows::UI::Color cardPressedOverlay{};
};

class ThemeManager
{
public:
    void Initialize(HWND hwnd);
    bool Refresh(HWND hwnd);

    // 将系统视觉属性（暗色模式、禁用圆角、无边框、无系统默认背板）应用到窗口
    void ApplyWindowBackdrop(HWND hwnd) const;

    AppThemeMode Mode() const { return mode_; }
    const AppSwitcherPalette& Palette() const { return palette_; }
    AppSwitcherPalette PaletteForActivationState(bool active) const;

private:
    static AppThemeMode ReadSystemTheme();
    static AppSwitcherPalette PaletteForTheme(AppThemeMode mode);

    AppThemeMode mode_ = AppThemeMode::Dark;
    AppSwitcherPalette palette_ = PaletteForTheme(AppThemeMode::Dark);
};
