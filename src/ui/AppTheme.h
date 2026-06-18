#pragma once

#include <winrt/Windows.UI.h>

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
};

enum class AppThemeMode
{
    Light,
    Dark,
};

AppSwitcherPalette PaletteForTheme(AppThemeMode mode);
