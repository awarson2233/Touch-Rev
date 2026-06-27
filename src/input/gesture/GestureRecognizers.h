#pragma once

#include "input/gesture/GestureContext.h"

#include <optional>
#include <vector>

namespace touchrev::gesture
{
struct GestureRecognitionResult
{
    GestureType type = GestureType::None;
    GesturePhase phase = GesturePhase::Update;
    Point delta{};
    Point center{};
    std::vector<GestureContact> contacts;
    std::int64_t timestamp = 0;
};

class ThreeFingerLongPressRecognizer
{
public:
    bool Enabled = true;
    double LongPressDurationMs = 400.0;
    double MaxStartMovement = 40.0;

    void OnBegin(const GestureContext& context);
    std::optional<GestureRecognitionResult> OnUpdate(const GestureContext& context);
    std::optional<GestureRecognitionResult> OnEnd(const GestureContext& context);
    void Reset();

private:
    enum class State
    {
        Idle,
        Pending,
        Tracking,
        Triggered,
    };

    double HardCancelMovementLimit() const;
    double TriggerMovementLimit(double elapsedMs) const;

    State state_ = State::Idle;
    Point lastCenter_{};
    std::int64_t triggerStartTimestamp_ = 0;
};

class MultiFingerTapRecognizer
{
public:
    explicit MultiFingerTapRecognizer(int requiredFingers);

    bool Enabled = true;
    double MaxTapDurationMs = 400.0;
    double MaxDoubleTapDelayMs = 450.0;
    double MinDoubleTapIntervalMs = 100.0;
    double MovementThreshold = 30.0;
    double DoubleTapDistance = 150.0;

    void OnBegin(const GestureContext& context);
    void OnUpdate(const GestureContext& context);
    std::optional<GestureRecognitionResult> OnEnd(const GestureContext& context);
    void ResetCurrentSession();
    void ResetAll();

private:
    int requiredFingers_ = 0;
    bool currentTapInvalid_ = false;
    bool hasFirstTap_ = false;
    std::int64_t firstTapTimestamp_ = 0;
    Point firstTapCenter_{};
};

}  // namespace touchrev::gesture
