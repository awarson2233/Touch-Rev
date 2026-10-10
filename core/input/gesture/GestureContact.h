#pragma once

#include "input/gesture/GestureTypes.h"
#include "input/raw/RawTouchInput.h"

#include <array>
#include <optional>
#include <vector>

namespace touchrev::gesture
{
struct GestureContact
{
    DWORD contactId = 0;
    ContactState state = ContactState::Started;
    Point start{};
    Point current{};
    Point previous{};
    LONG rawX = 0;
    LONG rawY = 0;
    LONG rawWidth = 0;
    LONG rawHeight = 0;
    std::int64_t startTimestamp = 0;
    std::int64_t timestamp = 0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    int frameCount = 0;

    double DeltaX() const { return current.x - start.x; }
    double DeltaY() const { return current.y - start.y; }
    double FrameDeltaX() const { return current.x - previous.x; }
    double FrameDeltaY() const { return current.y - previous.y; }
    double DistanceFromStart() const { return std::hypot(DeltaX(), DeltaY()); }
    double Speed() const { return std::hypot(velocityX, velocityY); }

    // 手势坐标统一为 0.1mm，阈值才能在不同分辨率的数字化仪上保持一致的物理含义。
    static GestureContact FromTouchPoint(const RawTouchInput::TouchPoint& point, const RawTouchInput::DeviceGeometry& geometry);
    void UpdateFromTouchPoint(const RawTouchInput::TouchPoint& point, const RawTouchInput::DeviceGeometry& geometry);
    void End();
};

struct GestureContactEvent
{
    enum class Kind
    {
        Started,
        Updated,
        Ended,
    };

    Kind kind = Kind::Updated;
    GestureContact contact{};
};

class GestureContactTracker
{
public:
    std::vector<GestureContactEvent> ProcessFrame(const RawTouchInput::Frame& frame);
    std::vector<GestureContact> GetActiveContacts() const;
    int ActiveCount() const;
    void Reset();

private:
    static constexpr size_t kMaxTrackedContacts = 256;

    std::array<std::optional<GestureContact>, kMaxTrackedContacts> activeContacts_{};
};

}  // namespace touchrev::gesture
