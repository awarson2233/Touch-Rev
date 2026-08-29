#include "LayoutEngine.h"
#include "common/GeometryUtils.h"

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

// 连续尺寸衰减曲线采样：基于幂律有理衰减模型
double SampleBaseScale(size_t count, bool isPortrait)
{
    if (count == 0)
    {
        return 0.16;
    }

    const double n = static_cast<double>(count);
    if (!isPortrait)
    {
        // 横屏参数：单卡最大占屏幕高度 32%，随数量平滑衰减至 13%
        constexpr double kSMax = 0.32;
        constexpr double kSMin = 0.13;
        constexpr double kAlpha = 0.38;
        constexpr double kGamma = 0.85;
        return kSMin + (kSMax - kSMin) / (1.0 + kAlpha * std::pow(n - 1.0, kGamma));
    }
    else
    {
        // 竖屏参数：单卡最大占屏幕宽度 44%，随数量平滑衰减至 16%
        constexpr double kSMax = 0.44;
        constexpr double kSMin = 0.16;
        constexpr double kAlpha = 0.40;
        constexpr double kGamma = 0.80;
        return kSMin + (kSMax - kSMin) / (1.0 + kAlpha * std::pow(n - 1.0, kGamma));
    }
}

struct LayoutCandidate
{
    size_t rowCount = 1;
    std::vector<size_t> rowCounts;
    double scale = 1.0;
    double totalWidth = 0.0;
    double totalHeight = 0.0;
    double cost = std::numeric_limits<double>::max();
};
}

namespace touchrev::appswitcher
{
LayoutResult LayoutEngine::Calculate(
    const std::vector<WindowItem>& windows,
    const RECT& workAreaPx,
    double scale,
    double paddingDip,
    double itemGapDip,
    double titleRowWeight,
    double contentRowWeight,
    double minAspect,
    double maxAspect)
{
    LayoutResult result;
    const size_t count = windows.size();
    if (count == 0)
    {
        return result;
    }

    const int workWidth = std::max(1, touchrev::common::RectWidth(workAreaPx));
    const int workHeight = std::max(1, touchrev::common::RectHeight(workAreaPx));
    const bool isPortrait = workHeight > workWidth;

    const double safeScale = std::max(0.01, scale);
    const double paddingPx = paddingDip * safeScale;
    const double gapPx = itemGapDip * safeScale;

    // 1. 连续曲线采样基础缩放比 sn
    const double sn = SampleBaseScale(count, isPortrait);

    // 2. 计算各卡片的基础宽高 (未缩放前)
    std::vector<SizeDouble> baseSizes;
    baseSizes.reserve(count);

    const double safeContentWeight = contentRowWeight > 0.0 ? contentRowWeight : 1.0;
    const double titleRatio = titleRowWeight / safeContentWeight;

    for (const auto& window : windows)
    {
        double aspect = 1.0;
        if (window.heightPx > 0.0)
        {
            aspect = window.widthPx / window.heightPx;
        }
        aspect = std::clamp(aspect, minAspect, maxAspect);

        double itemWidth = 0.0;
        double itemHeight = 0.0;
        if (!isPortrait)
        {
            const double thumbHeight = static_cast<double>(workHeight) * sn;
            const double titleHeight = static_cast<double>(workHeight) * titleRatio * 0.30;
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
        baseSizes.push_back({itemWidth, itemHeight});
    }

    // 3. 产生候选行划分并进行多目标代价评估
    const size_t maxPossibleRows = isPortrait
        ? std::min<size_t>(count, 6)
        : std::min<size_t>(count, count >= 16 ? 5 : 4);

    const double screenAspect = static_cast<double>(workWidth) / static_cast<double>(workHeight);
    const double targetAspect = isPortrait
        ? std::clamp(screenAspect * 1.05, 0.45, 0.90)
        : std::clamp(screenAspect * 0.95, 1.30, 2.40);

    const double maxAllowedWidth = static_cast<double>(workWidth) * 0.92;
    const double maxAllowedHeight = static_cast<double>(workHeight) * 0.88;
    const double maxContentW = std::max(10.0, maxAllowedWidth - paddingPx * 2.0);
    const double maxContentH = std::max(10.0, maxAllowedHeight - paddingPx * 2.0);

    LayoutCandidate bestCandidate;

    for (size_t r = 1; r <= maxPossibleRows; ++r)
    {
        LayoutCandidate cand;
        cand.rowCount = r;
        cand.rowCounts.resize(r);

        const size_t q = count / r;
        const size_t remainder = count % r;
        if (q == 0)
        {
            continue;
        }

        // 行均衡划分：保证任意两行卡片数差 <= 1
        for (size_t i = 0; i < r; ++i)
        {
            cand.rowCounts[i] = (i < remainder) ? (q + 1) : q;
        }

        // 模拟计算未缩放时的各行尺寸
        double candMaxRowWidth = 0.0;
        double candTotalRowHeight = 0.0;
        size_t windowIdx = 0;

        for (size_t i = 0; i < r; ++i)
        {
            const size_t rowLen = cand.rowCounts[i];
            double currentRowWidth = 0.0;
            double currentRowMaxH = 0.0;

            for (size_t c = 0; c < rowLen; ++c)
            {
                const auto& sz = baseSizes[windowIdx++];
                currentRowWidth += sz.width;
                if (c > 0)
                {
                    currentRowWidth += gapPx;
                }
                currentRowMaxH = std::max(currentRowMaxH, sz.height);
            }

            candMaxRowWidth = std::max(candMaxRowWidth, currentRowWidth);
            candTotalRowHeight += currentRowMaxH;
            if (i > 0)
            {
                candTotalRowHeight += gapPx;
            }
        }

        // 容纳比例缩放
        const double fitScaleX = (candMaxRowWidth > maxContentW) ? (maxContentW / candMaxRowWidth) : 1.0;
        const double fitScaleY = (candTotalRowHeight > maxContentH) ? (maxContentH / candTotalRowHeight) : 1.0;
        cand.scale = std::min(1.0, std::min(fitScaleX, fitScaleY));

        const double finalContentW = candMaxRowWidth * cand.scale;
        const double finalContentH = candTotalRowHeight * cand.scale;
        cand.totalWidth = finalContentW + paddingPx * 2.0;
        cand.totalHeight = finalContentH + paddingPx * 2.0;

        // 计算多目标代价
        const double currentAspect = cand.totalWidth / std::max(1.0, cand.totalHeight);
        const double aspectPenalty = std::abs(std::log(currentAspect) - std::log(targetAspect));

        // 缩放惩罚：过度缩小予以惩罚
        const double scalePenalty = (1.0 / std::max(0.01, cand.scale)) - 1.0;

        // 行不平衡惩罚 (轻微偏好多行整齐)
        const double balancePenalty = static_cast<double>(remainder * (r - remainder)) / static_cast<double>(r * r);

        // 偏好惩罚：小窗口不拆多行，多窗口不挤单行
        double layoutBiasPenalty = 0.0;
        if (count <= 2 && r > 1)
        {
            layoutBiasPenalty += 2.0;
        }
        else if (count >= 6 && r == 1)
        {
            layoutBiasPenalty += 1.8;
        }
        else if (count >= 12 && r <= 2 && !isPortrait)
        {
            layoutBiasPenalty += 0.8;
        }

        cand.cost = 1.6 * aspectPenalty + 3.2 * scalePenalty + 0.8 * balancePenalty + layoutBiasPenalty;

        if (cand.cost < bestCandidate.cost)
        {
            bestCandidate = cand;
        }
    }

    // 4. 基于最优候选构建精确像素布局
    result.items.resize(count);
    const double finalScale = bestCandidate.scale;
    const double effectiveGapPx = gapPx * finalScale;

    // 重新计算缩放后的各行卡片几何
    std::vector<double> rowWidths(bestCandidate.rowCount, 0.0);
    std::vector<double> rowHeights(bestCandidate.rowCount, 0.0);
    double maxSeenWidth = 0.0;
    size_t winIndex = 0;

    for (size_t r = 0; r < bestCandidate.rowCount; ++r)
    {
        const size_t rowLen = bestCandidate.rowCounts[r];
        double curRowW = 0.0;
        double curRowH = 0.0;

        for (size_t c = 0; c < rowLen; ++c)
        {
            const auto& sz = baseSizes[winIndex++];
            const double w = sz.width * finalScale;
            const double h = sz.height * finalScale;
            curRowW += w;
            if (c > 0)
            {
                curRowW += effectiveGapPx;
            }
            curRowH = std::max(curRowH, h);
        }

        rowWidths[r] = curRowW;
        rowHeights[r] = curRowH;
        maxSeenWidth = std::max(maxSeenWidth, curRowW);
    }

    // 布局卡片并执行行内水平居中
    double curY = 0.0;
    winIndex = 0;

    for (size_t r = 0; r < bestCandidate.rowCount; ++r)
    {
        const size_t rowLen = bestCandidate.rowCounts[r];
        const double rowW = rowWidths[r];
        const double offsetX = (maxSeenWidth - rowW) / 2.0;
        double curX = 0.0;

        for (size_t c = 0; c < rowLen; ++c)
        {
            const auto& sz = baseSizes[winIndex];
            const double w = std::round(sz.width * finalScale);
            const double h = std::round(sz.height * finalScale);

            const LONG left = static_cast<LONG>(std::lround(curX + offsetX + paddingPx));
            const LONG top = static_cast<LONG>(std::lround(curY + paddingPx));

            result.items[winIndex].rectPx = {
                left,
                top,
                static_cast<LONG>(left + w),
                static_cast<LONG>(top + h)};

            curX += w + effectiveGapPx;
            ++winIndex;
        }

        curY += rowHeights[r];
        if (r + 1 < bestCandidate.rowCount)
        {
            curY += effectiveGapPx;
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


