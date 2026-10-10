#pragma once

#include <windows.h>
#include <winrt/Windows.UI.h>

enum class AppThemeMode
{
    Light,
    Dark,
};

// 窗口背板材质。AppSwitcher 自绘亚克力，用 None 关掉系统背板；
// 配置窗口用 Mica，由 DWM 在窗口背后取样壁纸绘制。
enum class WindowBackdrop
{
    None,
    Mica,
};

class ThemeManager
{
public:
    void Initialize(HWND hwnd, WindowBackdrop backdrop = WindowBackdrop::None, bool roundedCorners = false);
    bool Refresh(HWND hwnd);

    // 将系统视觉属性（暗色模式、圆角策略、边框色、背板材质）应用到窗口
    void ApplyWindowBackdrop(HWND hwnd) const;

    AppThemeMode Mode() const { return mode_; }
    bool IsDark() const { return mode_ == AppThemeMode::Dark; }

private:
    static AppThemeMode ReadSystemTheme();

    AppThemeMode mode_ = AppThemeMode::Dark;
    WindowBackdrop backdrop_ = WindowBackdrop::None;
    bool roundedCorners_ = false;
};
