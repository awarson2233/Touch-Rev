#pragma once

#include <windows.h>
#include <winrt/Windows.UI.h>

enum class AppThemeMode
{
    Light,
    Dark,
};

class ThemeManager
{
public:
    void Initialize(HWND hwnd, bool isDialog = false);
    bool Refresh(HWND hwnd);

    // 将系统视觉属性（暗色模式、禁用圆角、无边框、无系统默认背板）应用到窗口
    void ApplyWindowBackdrop(HWND hwnd) const;

    AppThemeMode Mode() const { return mode_; }

private:
    static AppThemeMode ReadSystemTheme();

    AppThemeMode mode_ = AppThemeMode::Dark;
    bool isDialog_ = false;
};
