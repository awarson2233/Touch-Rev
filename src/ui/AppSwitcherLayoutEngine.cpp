#include "AppSwitcherLayoutEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
struct SizeDouble
{
    double width = 0.0;
    double height = 0.0;
};

struct RowItem
{
    size_t index = 0;
    RECT rect{};
};

int WidthOf(const RECT& rect)
{
    return rect.right - rect.left;
}

int HeightOf(const RECT& rect)
{
    return rect.bottom - rect.top;
}
}

AppSwitcherLayoutResult AppSwitcherLayoutEngine::Calculate(
    const std::vector<AppSwitcherWindowItem>& windows,
    const RECT& workAreaPx,
    double scale)
{
    AppSwitcherLayoutResult result;
    const size_t count = windows.size();
    if (count == 0)
    {
        return result;
    }

    const int workWidth = std::max(1, WidthOf(workAreaPx));
    const int workHeight = std::max(1, HeightOf(workAreaPx));
    const bool isPortrait = workHeight > workWidth;

    double sn = 0.16;
    if (!isPortrait)
    {
        sn = count <= 2 ? 0.30 : count <= 5 ? 0.24 : count <= 10 ? 0.20 : 0.16;
    }
    else
    {
        sn = count <= 2 ? 0.42 : count <= 5 ? 0.26 : count <= 10 ? 0.20 : 0.16;
    }

    const double safeScale = std::max(0.01, scale);
    const double paddingPx = PaddingDip * safeScale;
    const double gapPx = ItemGapDip * safeScale;

    std::vector<SizeDouble> itemSizes;
    itemSizes.reserve(count);
    double totalArea = 0.0;

    for (const auto& window : windows)
    {
        double aspect = 1.0;
        if (window.heightPx > 0.0)
        {
            aspect = window.widthPx / window.heightPx;
        }
        aspect = std::clamp(aspect, MinAspect, MaxAspect);

        double itemWidth = 0.0;
        double itemHeight = 0.0;
        if (!isPortrait)
        {
            const double thumbHeight = static_cast<double>(workHeight) * sn;
            const double titleHeight = thumbHeight * (1.8 / 8.2);
            itemHeight = thumbHeight + titleHeight;
            itemWidth = thumbHeight * aspect;
        }
        else
        {
            const double maxAllowedItemHeight = static_cast<double>(workHeight) * 0.5;
            const double targetWidth = static_cast<double>(workWidth) * sn;
            const double thumbHeightByWidth = targetWidth / aspect;
            const double itemHeightByWidth = thumbHeightByWidth * 1.25;

            if (itemHeightByWidth <= maxAllowedItemHeight)
            {
                itemWidth = targetWidth;
                itemHeight = itemHeightByWidth;
            }
            else
            {
                itemHeight = maxAllowedItemHeight;
                const double thumbHeight = itemHeight / 1.25;
                itemWidth = thumbHeight * aspect;
            }
        }

        itemHeight += 2.0 * safeScale;
        itemSizes.push_back({std::round(itemWidth), std::round(itemHeight)});
        totalArea += itemWidth * itemHeight;
    }

    const auto maxWidthIt = std::max_element(
        itemSizes.begin(),
        itemSizes.end(),
        [](const SizeDouble& lhs, const SizeDouble& rhs) { return lhs.width < rhs.width; });
    const double screenAspect = static_cast<double>(workWidth) / static_cast<double>(workHeight);
    const double idealWidth = std::sqrt(totalArea * screenAspect * 2.2);
    const double wrappingWidth = std::min(
        static_cast<double>(workWidth) * 0.95,
        std::max(maxWidthIt != itemSizes.end() ? maxWidthIt->width : 1.0, idealWidth));

    std::vector<std::vector<RowItem>> rows;
    std::vector<RowItem> currentRow;
    double curX = 0.0;
    double curY = 0.0;
    double rowMaxHeight = 0.0;
    double maxSeenWidth = 0.0;

    for (size_t i = 0; i < count; ++i)
    {
        const auto& size = itemSizes[i];
        if (!currentRow.empty() && curX + size.width > wrappingWidth)
        {
            rows.push_back(currentRow);
            maxSeenWidth = std::max(maxSeenWidth, curX - gapPx);
            curX = 0.0;
            curY += rowMaxHeight + gapPx;
            rowMaxHeight = 0.0;
            currentRow.clear();
        }

        RECT rect{
            static_cast<LONG>(std::lround(curX)),
            static_cast<LONG>(std::lround(curY)),
            static_cast<LONG>(std::lround(curX + size.width)),
            static_cast<LONG>(std::lround(curY + size.height))};
        currentRow.push_back({i, rect});
        curX += size.width + gapPx;
        rowMaxHeight = std::max(rowMaxHeight, size.height);
    }

    if (!currentRow.empty())
    {
        rows.push_back(currentRow);
        maxSeenWidth = std::max(maxSeenWidth, curX - gapPx);
        curY += rowMaxHeight;
    }

    result.items.resize(count);
    for (const auto& row : rows)
    {
        if (row.empty())
        {
            continue;
        }

        const RECT& first = row.front().rect;
        const RECT& last = row.back().rect;
        const double rowWidth = static_cast<double>(last.right - first.left);
        const double offsetX = (maxSeenWidth - rowWidth) / 2.0;

        for (const auto& item : row)
        {
            const RECT& r = item.rect;
            const int width = WidthOf(r);
            const int height = HeightOf(r);
            const LONG left = static_cast<LONG>(std::lround(static_cast<double>(r.left) + offsetX + paddingPx));
            const LONG top = static_cast<LONG>(std::lround(static_cast<double>(r.top) + paddingPx));
            result.items[item.index].rectPx = {left, top, left + width, top + height};
        }
    }

    result.totalSizePx = {
        static_cast<LONG>(std::ceil(maxSeenWidth + paddingPx * 2.0)),
        static_cast<LONG>(std::ceil(curY + paddingPx * 2.0))};
    return result;
}
