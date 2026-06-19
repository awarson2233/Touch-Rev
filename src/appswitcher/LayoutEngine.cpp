#include "LayoutEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

namespace touchrev::appswitcher
{
LayoutResult LayoutEngine::Calculate(
    const std::vector<WindowItem>& windows,
    const RECT& workAreaPx,
    double scale)
{
    LayoutResult result;
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
        sn = count <= 2 ? 0.30 : count <= 5 ? 0.24 : count <= 10 ? 0.18 : 0.16;
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
            const double titleHeight = static_cast<double>(workHeight) * (TitleRowWeight / ContentRowWeight) * 0.30;
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
    const double idealWidth = std::sqrt(totalArea * screenAspect * 2.0);
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

size_t LayoutEngine::CalculateNextSelection(
    const std::vector<ItemGeometry>& items,
    size_t currentIndex,
    int stepX,
    int stepY)
{
    if (currentIndex == static_cast<size_t>(-1) || currentIndex >= items.size() || (stepX == 0 && stepY == 0))
    {
        return static_cast<size_t>(-1);
    }

    const auto& current = items[currentIndex];
    const double currentCenterX = current.position.x + current.size.width * 0.5;
    const double currentCenterY = current.position.y + current.size.height * 0.5;
    size_t bestIndex = static_cast<size_t>(-1);
    double bestDistance = std::numeric_limits<double>::max();

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i == currentIndex || !items[i].visible)
        {
            continue;
        }

        const auto& candidate = items[i];
        const double candidateCenterX = candidate.position.x + candidate.size.width * 0.5;
        const double candidateCenterY = candidate.position.y + candidate.size.height * 0.5;
        const double dx = candidateCenterX - currentCenterX;
        const double dy = candidateCenterY - currentCenterY;
        bool matchesDirection = false;

        if (stepX > 0 && dx > 10.0 && std::abs(dy) < candidate.size.height)
        {
            matchesDirection = true;
        }
        else if (stepX < 0 && dx < -10.0 && std::abs(dy) < candidate.size.height)
        {
            matchesDirection = true;
        }
        else if (stepY > 0 && dy > 10.0 && std::abs(dx) < candidate.size.width)
        {
            matchesDirection = true;
        }
        else if (stepY < 0 && dy < -10.0 && std::abs(dx) < candidate.size.width)
        {
            matchesDirection = true;
        }

        if (!matchesDirection)
        {
            continue;
        }

        const double distance = dx * dx + dy * dy;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    return bestIndex;
}

size_t LayoutEngine::GetNextVisibleIndex(
    const std::vector<ItemGeometry>& items,
    size_t currentIndex,
    bool forward)
{
    if (items.empty())
    {
        return static_cast<size_t>(-1);
    }

    if (currentIndex == static_cast<size_t>(-1) || currentIndex >= items.size())
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].visible)
            {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    }

    if (forward)
    {
        for (size_t i = currentIndex + 1; i < items.size(); ++i)
        {
            if (items[i].visible)
            {
                return i;
            }
        }
        for (size_t i = 0; i < currentIndex; ++i)
        {
            if (items[i].visible)
            {
                return i;
            }
        }
    }
    else
    {
        for (size_t i = currentIndex; i > 0; --i)
        {
            const size_t candidate = i - 1;
            if (items[candidate].visible)
            {
                return candidate;
            }
        }
        for (size_t i = items.size(); i > currentIndex + 1; --i)
        {
            const size_t candidate = i - 1;
            if (items[candidate].visible)
            {
                return candidate;
            }
        }
    }
    return static_cast<size_t>(-1);
}

size_t LayoutEngine::FindColumnExtreme(
    const std::vector<ItemGeometry>& items,
    size_t currentIndex,
    bool findMaxY)
{
    if (currentIndex == static_cast<size_t>(-1) || currentIndex >= items.size())
    {
        return static_cast<size_t>(-1);
    }

    const auto& current = items[currentIndex];
    const double currentCenterX = current.position.x + current.size.width * 0.5;
    
    size_t bestIndex = currentIndex;
    double extremeY = current.position.y;

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i == currentIndex || !items[i].visible)
        {
            continue;
        }

        const auto& candidate = items[i];
        const double candidateCenterX = candidate.position.x + candidate.size.width * 0.5;
        const double dx = candidateCenterX - currentCenterX;

        // 如果在水平方向上有重合，则视为同一列
        if (std::abs(dx) < candidate.size.width)
        {
            if (findMaxY)
            {
                if (candidate.position.y > extremeY)
                {
                    extremeY = candidate.position.y;
                    bestIndex = i;
                }
            }
            else
            {
                if (candidate.position.y < extremeY)
                {
                    extremeY = candidate.position.y;
                    bestIndex = i;
                }
            }
        }
    }

    return bestIndex;
}
}


