#include "input/gesture/GestureContact.h"

#include <algorithm>

namespace touchrev::gesture
{
namespace
{
Point ToGesturePoint(const RawTouchInput::TouchPoint& point)
{
    return {static_cast<double>(point.x) * 0.1, static_cast<double>(point.y) * 0.1};
}

bool IsActivePoint(const RawTouchInput::TouchPoint& point)
{
    return point.active || point.state == RawTouchInput::PointState::Pressed || point.state == RawTouchInput::PointState::Moved;
}
}

GestureContact GestureContact::FromTouchPoint(const RawTouchInput::TouchPoint& point)
{
    const Point gesturePoint = ToGesturePoint(point);
    return {
        .contactId = point.contactId,
        .state = ContactState::Started,
        .start = gesturePoint,
        .current = gesturePoint,
        .previous = gesturePoint,
        .rawX = point.x,
        .rawY = point.y,
        .rawWidth = point.width,
        .rawHeight = point.height,
        .startTimestamp = point.timestamp,
        .timestamp = point.timestamp,
        .frameCount = 1,
    };
}

void GestureContact::UpdateFromTouchPoint(const RawTouchInput::TouchPoint& point)
{
    previous = current;
    current = ToGesturePoint(point);
    rawX = point.x;
    rawY = point.y;
    rawWidth = point.width;
    rawHeight = point.height;

    const std::int64_t delta = point.timestamp - timestamp;
    const double deltaMs = CounterMs(delta);
    if (deltaMs >= 1.0)
    {
        velocityX = FrameDeltaX() / deltaMs;
        velocityY = FrameDeltaY() / deltaMs;
    }

    timestamp = point.timestamp;
    state = ContactState::Active;
    ++frameCount;
}

void GestureContact::End()
{
    state = ContactState::Ended;
}

std::vector<GestureContactEvent> GestureContactTracker::ProcessFrame(const RawTouchInput::Frame& frame)
{
    std::vector<GestureContactEvent> events;
    std::array<bool, kMaxTrackedContacts> activeInFrame{};

    for (const RawTouchInput::TouchPoint& point : frame.points)
    {
        if (point.contactId >= kMaxTrackedContacts)
        {
            continue;
        }

        if (IsActivePoint(point))
        {
            activeInFrame[point.contactId] = true;
        }

        std::optional<GestureContact>& active = activeContacts_[point.contactId];
        if (point.state == RawTouchInput::PointState::Pressed)
        {
            if (active.has_value())
            {
                GestureContact stale = *active;
                stale.End();
                events.push_back({.kind = GestureContactEvent::Kind::Ended, .contact = stale});
            }

            active = GestureContact::FromTouchPoint(point);
            events.push_back({.kind = GestureContactEvent::Kind::Started, .contact = *active});
        }
        else if (point.state == RawTouchInput::PointState::Moved)
        {
            if (active.has_value())
            {
                active->UpdateFromTouchPoint(point);
                events.push_back({.kind = GestureContactEvent::Kind::Updated, .contact = *active});
            }
            else
            {
                active = GestureContact::FromTouchPoint(point);
                events.push_back({.kind = GestureContactEvent::Kind::Started, .contact = *active});
            }
        }
        else if (point.state == RawTouchInput::PointState::Released)
        {
            if (active.has_value())
            {
                active->UpdateFromTouchPoint(point);
                active->End();
                events.push_back({.kind = GestureContactEvent::Kind::Ended, .contact = *active});
                active.reset();
            }
        }
    }

    if (frame.frameSync)
    {
        for (size_t id = 0; id < activeContacts_.size(); ++id)
        {
            if (!activeContacts_[id].has_value() || activeInFrame[id])
            {
                continue;
            }

            GestureContact ended = *activeContacts_[id];
            ended.timestamp = frame.timestamp;
            ended.End();
            events.push_back({.kind = GestureContactEvent::Kind::Ended, .contact = ended});
            activeContacts_[id].reset();
        }
    }

    return events;
}

std::vector<GestureContact> GestureContactTracker::GetActiveContacts() const
{
    std::vector<GestureContact> contacts;
    contacts.reserve(static_cast<size_t>(ActiveCount()));
    for (const auto& contact : activeContacts_)
    {
        if (contact.has_value())
        {
            contacts.push_back(*contact);
        }
    }
    return contacts;
}

int GestureContactTracker::ActiveCount() const
{
    return static_cast<int>(std::count_if(activeContacts_.begin(), activeContacts_.end(), [](const auto& contact) {
        return contact.has_value();
    }));
}

void GestureContactTracker::Reset()
{
    for (auto& contact : activeContacts_)
    {
        contact.reset();
    }
}

}  // namespace touchrev::gesture
