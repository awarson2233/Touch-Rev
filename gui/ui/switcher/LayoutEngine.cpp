#include "LayoutEngine.h"
#include "common/GeometryUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
// 缩略图高度上限占工作区短边的比例：单卡 28%，随数量平滑衰减到 12%（幂律有理衰减）。
double SampleThumbnailScale(size_t count)
{
    constexpr double kSMax = 0.28;
    constexpr double kSMin = 0.12;
    constexpr double kAlpha = 0.45;
    constexpr double kGamma = 0.85;
    if (count <= 1)
    {
        return kSMax;
    }
    const double n = static_cast<double>(count);
    return kSMin + (kSMax - kSMin) / (1.0 + kAlpha * std::pow(n - 1.0, kGamma));
}

// 按顺序把卡片切成 rowCount 行，使"最宽的一行"尽量窄（宽度以缩略图高度为单位：Σ宽高比 + 间距）。
// 多个划分同样均衡时，让后面的行多放卡片，避免卡片都挤在第一行。
std::vector<size_t> PartitionRows(const std::vector<double>& aspects, size_t rowCount, double gapUnits)
{
    const size_t count = aspects.size();
    std::vector<double> prefix(count + 1, 0.0);
    for (size_t i = 0; i < count; ++i)
    {
        prefix[i + 1] = prefix[i] + aspects[i];
    }

    const auto rowWidth = [&](size_t begin, size_t end) {
        return prefix[end] - prefix[begin] + gapUnits * static_cast<double>(end - begin - 1);
    };

    constexpr double kInf = std::numeric_limits<double>::max();
    constexpr double kEpsilon = 1e-9;
    // cost[j][i]：前 i 张卡分成 j 行时最宽一行的宽度；split[j][i] 记录最后一行的起点。
    std::vector<std::vector<double>> cost(rowCount + 1, std::vector<double>(count + 1, kInf));
    std::vector<std::vector<size_t>> split(rowCount + 1, std::vector<size_t>(count + 1, 0));
    cost[0][0] = 0.0;

    for (size_t j = 1; j <= rowCount; ++j)
    {
        for (size_t i = j; i <= count; ++i)
        {
            // k 从小到大遍历、只在严格更优时替换：平局时保留更小的 k，即最后一行更长。
            for (size_t k = j - 1; k < i; ++k)
            {
                if (cost[j - 1][k] == kInf)
                {
                    continue;
                }
                const double candidate = std::max(cost[j - 1][k], rowWidth(k, i));
                if (candidate < cost[j][i] - kEpsilon)
                {
                    cost[j][i] = candidate;
                    split[j][i] = k;
                }
            }
        }
    }

    std::vector<size_t> rowCounts(rowCount, 0);
    size_t end = count;
    for (size_t j = rowCount; j > 0; --j)
    {
        const size_t begin = split[j][end];
        rowCounts[j - 1] = end - begin;
        end = begin;
    }
    return rowCounts;
}

struct LayoutCandidate
{
    std::vector<size_t> rowCounts;
    double thumbHeight = 0.0;
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
    double titleHeightDip,
    double cardBorderDip,
    double minAspect,
    double maxAspect)
{
    LayoutResult result;
    const size_t count = windows.size();
    if (count == 0)
    {
        return result;
    }

    const double workWidth = static_cast<double>(std::max(1, touchrev::common::RectWidth(workAreaPx)));
    const double workHeight = static_cast<double>(std::max(1, touchrev::common::RectHeight(workAreaPx)));

    const double safeScale = std::max(0.01, scale);
    const double paddingPx = paddingDip * safeScale;
    const double gapPx = itemGapDip * safeScale;
    // 卡片 = 两侧描边 + 缩略图区（宽高比等于窗口），竖直方向再加固定高度的标题行。
    // 标题行与描边在模板里是 DIP，按当前 DPI 换算成像素，缩略图区才正好是窗口的宽高比。
    const double borderPx = 2.0 * std::max(0.0, cardBorderDip) * safeScale;
    const double titlePx = std::max(0.0, titleHeightDip) * safeScale;

    std::vector<double> aspects;
    aspects.reserve(count);
    for (const auto& window : windows)
    {
        const double aspect = window.heightPx > 0.0 ? window.widthPx / window.heightPx : 1.0;
        aspects.push_back(std::clamp(aspect, minAspect, maxAspect));
    }

    const double availableWidth = std::max(10.0, workWidth * 0.92 - paddingPx * 2.0);
    const double availableHeight = std::max(10.0, workHeight * 0.88 - paddingPx * 2.0);
    const double thumbHeightCap = std::min(workWidth, workHeight) * SampleThumbnailScale(count);

    // 对每个行数求能放下的最大缩略图高度，取卡片最大的方案；
    // 差距在 2% 以内时取行数少的，能排成一行就不拆行。
    const size_t maxRows = std::min<size_t>(count, 6);
    const double gapUnits = gapPx / std::max(1.0, thumbHeightCap);
    LayoutCandidate best;
    for (size_t rowCount = 1; rowCount <= maxRows; ++rowCount)
    {
        LayoutCandidate candidate;
        candidate.rowCounts = PartitionRows(aspects, rowCount, gapUnits);

        double heightByWidth = std::numeric_limits<double>::max();
        size_t index = 0;
        for (const size_t rowLen : candidate.rowCounts)
        {
            double aspectSum = 0.0;
            for (size_t c = 0; c < rowLen; ++c)
            {
                aspectSum += aspects[index++];
            }
            const double rowFixed = gapPx * static_cast<double>(rowLen - 1) + borderPx * static_cast<double>(rowLen);
            heightByWidth = std::min(heightByWidth, (availableWidth - rowFixed) / std::max(0.01, aspectSum));
        }

        const double rows = static_cast<double>(rowCount);
        const double heightByHeight =
            (availableHeight - gapPx * (rows - 1.0) - (borderPx + titlePx) * rows) / rows;

        candidate.thumbHeight = std::max(1.0, std::min({thumbHeightCap, heightByWidth, heightByHeight}));
        if (best.rowCounts.empty() || candidate.thumbHeight > best.thumbHeight * 1.02)
        {
            best = std::move(candidate);
        }
    }

    // 按选中的方案摆放：所有卡片同高，每行水平居中，行间等距。
    const double thumbHeight = best.thumbHeight;
    const double itemHeight = std::round(thumbHeight + titlePx + borderPx);

    std::vector<double> rowWidths;
    rowWidths.reserve(best.rowCounts.size());
    double maxRowWidth = 0.0;
    size_t index = 0;
    for (const size_t rowLen : best.rowCounts)
    {
        double rowWidth = gapPx * static_cast<double>(rowLen - 1);
        for (size_t c = 0; c < rowLen; ++c)
        {
            rowWidth += std::round(thumbHeight * aspects[index++] + borderPx);
        }
        rowWidths.push_back(rowWidth);
        maxRowWidth = std::max(maxRowWidth, rowWidth);
    }

    result.items.resize(count);
    double curY = 0.0;
    index = 0;
    for (size_t r = 0; r < best.rowCounts.size(); ++r)
    {
        double curX = (maxRowWidth - rowWidths[r]) / 2.0;
        for (size_t c = 0; c < best.rowCounts[r]; ++c)
        {
            const double itemWidth = std::round(thumbHeight * aspects[index] + borderPx);
            const LONG left = static_cast<LONG>(std::lround(curX + paddingPx));
            const LONG top = static_cast<LONG>(std::lround(curY + paddingPx));
            result.items[index].rectPx = {
                left,
                top,
                static_cast<LONG>(left + itemWidth),
                static_cast<LONG>(top + itemHeight)};

            curX += itemWidth + gapPx;
            ++index;
        }

        curY += itemHeight;
        if (r + 1 < best.rowCounts.size())
        {
            curY += gapPx;
        }
    }

    result.totalSizePx = {
        static_cast<LONG>(std::ceil(maxRowWidth + paddingPx * 2.0)),
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
