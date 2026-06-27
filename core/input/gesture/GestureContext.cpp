#include "input/gesture/GestureContext.h"

#include <utility>

namespace touchrev::gesture
{
GestureContext::GestureContext(
    std::vector<GestureContact> contacts,
    std::int64_t startTimestamp,
    std::int64_t currentTimestamp,
    Point anchor)
    : contacts_(std::move(contacts)),
      startTimestamp_(startTimestamp),
      currentTimestamp_(currentTimestamp),
      anchor_(anchor)
{
    if (contacts_.empty())
    {
        return;
    }

    Point sumCenter{};
    Point sumDelta{};
    Point sumFrameDelta{};
    Point sumVelocity{};
    double sumSpeed = 0.0;

    for (const GestureContact& contact : contacts_)
    {
        sumCenter.x += contact.current.x;
        sumCenter.y += contact.current.y;
        sumDelta.x += contact.DeltaX();
        sumDelta.y += contact.DeltaY();
        sumFrameDelta.x += contact.FrameDeltaX();
        sumFrameDelta.y += contact.FrameDeltaY();
        sumVelocity.x += contact.velocityX;
        sumVelocity.y += contact.velocityY;
        sumSpeed += contact.Speed();
    }

    const double count = static_cast<double>(contacts_.size());
    center_ = {sumCenter.x / count, sumCenter.y / count};
    averageDelta_ = {sumDelta.x / count, sumDelta.y / count};
    averageFrameDelta_ = {sumFrameDelta.x / count, sumFrameDelta.y / count};
    averageVelocity_ = {sumVelocity.x / count, sumVelocity.y / count};
    averageSpeed_ = sumSpeed / count;
    totalDistance_ = std::hypot(averageDelta_.x, averageDelta_.y);
}

}  // namespace touchrev::gesture
