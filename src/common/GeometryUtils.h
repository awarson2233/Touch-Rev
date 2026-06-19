#pragma once

#include "CoordinateSpace.h"
#include <windows.h>
#include <cmath>
#include <algorithm>

namespace touchrev::common
{
inline int RectWidth(const RECT& rect)
{
    return rect.right - rect.left;
}

inline int RectHeight(const RECT& rect)
{
    return rect.bottom - rect.top;
}

inline double Distance(PointDip a, PointDip b)
{
    const double dx = static_cast<double>(a.x - b.x);
    const double dy = static_cast<double>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

inline RECT ScaleToPx(PointDip origin, SizeDip size, double scale)
{
    const double safeScale = std::max(0.01, scale);
    return {
        static_cast<LONG>(std::floor(static_cast<double>(origin.x) * safeScale)),
        static_cast<LONG>(std::floor(static_cast<double>(origin.y) * safeScale)),
        static_cast<LONG>(std::ceil(static_cast<double>(origin.x + size.width) * safeScale)),
        static_cast<LONG>(std::ceil(static_cast<double>(origin.y + size.height) * safeScale))};
}
}
