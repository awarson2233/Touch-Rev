#pragma once

#include "CoordinateSpace.h"
#include <windows.h>
#include <cmath>

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
}
