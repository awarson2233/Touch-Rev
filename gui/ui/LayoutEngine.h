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
        double scale,
        double paddingDip,
        double itemGapDip,
        double titleRowWeight,
        double contentRowWeight,
        double minAspect,
        double maxAspect);

    static size_t CalculateNextSelection(
        const std::vector<ItemGeometry>& items,
        size_t currentIndex,
        int stepX,
        int stepY);

    static size_t GetNextVisibleIndex(
        const std::vector<ItemGeometry>& items,
        size_t currentIndex,
        bool forward);

    static size_t FindColumnExtreme(
        const std::vector<ItemGeometry>& items,
        size_t currentIndex,
        bool findMaxY);
};
}


