#pragma once

#include <windows.h>

#include <cmath>
#include <cstdint>

namespace touchrev::gesture
{
struct Point
{
    double x = 0.0;
    double y = 0.0;
};

enum class ContactState
{
    Started,
    Active,
    Ended,
};

enum class GesturePhase
{
    Begin,
    Update,
    End,
    Cancelled,
};

enum class GestureType
{
    None,
    ThreeFingerLongPress,
    ThreeFingerDoubleTap,
};

enum class GestureSessionPhase
{
    Pending,
    Active,
    Completed,
    Cancelled,
};

enum class GestureEndReason
{
    Unknown,
    AllFingersUp,
    QuorumLostTimeout,
    RecognitionTimeout,
    WatchdogTimeout,
};

inline std::int64_t CounterNow()
{
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

inline double CounterMs(std::int64_t delta)
{
    static const double frequency = []() {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();

    return static_cast<double>(delta) * 1000.0 / frequency;
}

inline double Distance(Point a, Point b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

inline Point AveragePoint(Point a, Point b)
{
    return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
}

}  // namespace touchrev::gesture
