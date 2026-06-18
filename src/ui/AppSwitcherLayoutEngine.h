#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct AppSwitcherWindowItem
{
    HWND hwnd = nullptr;
    double widthPx = 1280.0;
    double heightPx = 720.0;
    std::wstring title;
};

struct AppSwitcherItemLayout
{
    RECT rectPx{};
};

struct AppSwitcherLayoutResult
{
    SIZE totalSizePx{};
    std::vector<AppSwitcherItemLayout> items;
};

class AppSwitcherLayoutEngine
{
public:
    static AppSwitcherLayoutResult Calculate(
        const std::vector<AppSwitcherWindowItem>& windows,
        const RECT& workAreaPx,
        double scale);

    static constexpr double ItemGapDip = 32.0;
    static constexpr double PaddingDip = 48.0;
    static constexpr double MinAspect = 0.4;
    static constexpr double MaxAspect = 2.5;
};
