#pragma once

#include "common/CoordinateSpace.h"
#include <windows.h>

#include <string>
#include <vector>

namespace touchrev::appswitcher
{
struct WindowItem
{
    HWND hwnd = nullptr;
    double widthPx = 1280.0;
    double heightPx = 720.0;
    std::wstring title;
};

struct ItemLayout
{
    RECT rectPx{};
};

struct LayoutResult
{
    SIZE totalSizePx{};
    std::vector<ItemLayout> items;
};

struct ItemGeometry
{
    PointDip position;
    SizeDip size;
    bool visible = false;
};

class LayoutEngine
{
public:
    static LayoutResult Calculate(
        const std::vector<WindowItem>& windows,
        const RECT& workAreaPx,
        double scale);

    static size_t CalculateNextSelection(
        const std::vector<ItemGeometry>& items,
        size_t currentIndex,
        int stepX,
        int stepY);

    static size_t GetNextVisibleIndex(
        const std::vector<ItemGeometry>& items,
        size_t currentIndex,
        bool forward);


    static constexpr double ItemGapDip = 32.0;
    static constexpr double PaddingDip = 48.0;
    static constexpr double TitleRowWeight = 1.6;
    static constexpr double ContentRowWeight = 8.4;
    static constexpr double TotalRowWeight = TitleRowWeight + ContentRowWeight;
    static constexpr double MinAspect = 0.4;
    static constexpr double MaxAspect = 2.5;
};
}


