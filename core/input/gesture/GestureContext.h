#pragma once

#include "input/gesture/GestureContact.h"

#include <vector>

namespace touchrev::gesture
{
class GestureContext
{
public:
    GestureContext(
        std::vector<GestureContact> contacts,
        std::int64_t startTimestamp,
        std::int64_t currentTimestamp,
        Point anchor);

    const std::vector<GestureContact>& Contacts() const { return contacts_; }
    int FingerCount() const { return static_cast<int>(contacts_.size()); }

    Point Center() const { return center_; }
    Point AverageDelta() const { return averageDelta_; }
    Point AverageFrameDelta() const { return averageFrameDelta_; }
    Point AverageVelocity() const { return averageVelocity_; }
    Point Anchor() const { return anchor_; }
    Point DeltaFromAnchor() const { return {center_.x - anchor_.x, center_.y - anchor_.y}; }

    double AverageSpeed() const { return averageSpeed_; }
    double TotalDistance() const { return totalDistance_; }
    double DurationMs() const { return CounterMs(currentTimestamp_ - startTimestamp_); }
    bool IsStationary() const { return totalDistance_ < kStationaryThreshold; }

    std::int64_t StartTimestamp() const { return startTimestamp_; }
    std::int64_t CurrentTimestamp() const { return currentTimestamp_; }

    static constexpr double kStationaryThreshold = 20.0;

private:
    std::vector<GestureContact> contacts_;
    std::int64_t startTimestamp_ = 0;
    std::int64_t currentTimestamp_ = 0;
    Point anchor_{};
    Point center_{};
    Point averageDelta_{};
    Point averageFrameDelta_{};
    Point averageVelocity_{};
    double averageSpeed_ = 0.0;
    double totalDistance_ = 0.0;
};

}  // namespace touchrev::gesture
