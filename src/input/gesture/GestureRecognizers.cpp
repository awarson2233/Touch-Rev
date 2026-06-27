#include "input/gesture/GestureRecognizers.h"

#include <algorithm>

namespace touchrev::gesture
{
namespace
{
GestureRecognitionResult MakeResult(
    GestureType type,
    GesturePhase phase,
    Point delta,
    const GestureContext& context)
{
    return {
        .type = type,
        .phase = phase,
        .delta = delta,
        .center = context.Center(),
        .contacts = context.Contacts(),
        .timestamp = context.CurrentTimestamp(),
    };
}
}

void ThreeFingerLongPressRecognizer::OnBegin(const GestureContext& context)
{
    state_ = State::Pending;
    lastCenter_ = context.Center();
    triggerStartTimestamp_ = context.CurrentTimestamp();
}

std::optional<GestureRecognitionResult> ThreeFingerLongPressRecognizer::OnUpdate(const GestureContext& context)
{
    if (!Enabled)
    {
        return std::nullopt;
    }

    const double elapsedMs = CounterMs(context.CurrentTimestamp() - triggerStartTimestamp_);
    switch (state_)
    {
    case State::Pending:
    {
        if (context.FingerCount() != 3)
        {
            return std::nullopt;
        }

        const Point deltaFromAnchor = context.DeltaFromAnchor();
        const double distanceFromAnchor = std::hypot(deltaFromAnchor.x, deltaFromAnchor.y);
        if (distanceFromAnchor > HardCancelMovementLimit())
        {
            state_ = State::Tracking;
            return std::nullopt;
        }

        if (elapsedMs >= LongPressDurationMs)
        {
            if (distanceFromAnchor <= TriggerMovementLimit(elapsedMs))
            {
                state_ = State::Triggered;
                lastCenter_ = context.Center();
                return MakeResult(GestureType::ThreeFingerLongPress, GesturePhase::Begin, {}, context);
            }

            state_ = State::Tracking;
        }
        break;
    }
    case State::Triggered:
    {
        if (context.FingerCount() == 0)
        {
            return std::nullopt;
        }

        const Point center = context.Center();
        const Point delta{center.x - lastCenter_.x, center.y - lastCenter_.y};
        lastCenter_ = center;
        return MakeResult(GestureType::ThreeFingerLongPress, GesturePhase::Update, delta, context);
    }
    case State::Tracking:
    case State::Idle:
        break;
    }

    return std::nullopt;
}

std::optional<GestureRecognitionResult> ThreeFingerLongPressRecognizer::OnEnd(const GestureContext& context)
{
    if (state_ == State::Triggered)
    {
        state_ = State::Idle;
        return MakeResult(GestureType::ThreeFingerLongPress, GesturePhase::End, {}, context);
    }

    Reset();
    return std::nullopt;
}

void ThreeFingerLongPressRecognizer::Reset()
{
    state_ = State::Idle;
    lastCenter_ = {};
    triggerStartTimestamp_ = 0;
}

double ThreeFingerLongPressRecognizer::HardCancelMovementLimit() const
{
    return MaxStartMovement * 2.4;
}

double ThreeFingerLongPressRecognizer::TriggerMovementLimit(double elapsedMs) const
{
    const double duration = std::max(1.0, LongPressDurationMs);
    const double progress = std::clamp(elapsedMs / duration, 0.0, 1.0);
    return MaxStartMovement * (1.15 + (0.35 * progress));
}

MultiFingerTapRecognizer::MultiFingerTapRecognizer(int requiredFingers)
    : requiredFingers_(requiredFingers)
{
}

void MultiFingerTapRecognizer::OnBegin(const GestureContext&)
{
    currentTapInvalid_ = false;
}

void MultiFingerTapRecognizer::OnUpdate(const GestureContext& context)
{
    if (!Enabled)
    {
        return;
    }

    if (context.TotalDistance() > MovementThreshold)
    {
        currentTapInvalid_ = true;
    }
}

std::optional<GestureRecognitionResult> MultiFingerTapRecognizer::OnEnd(const GestureContext& context)
{
    if (!Enabled || currentTapInvalid_ || context.FingerCount() != requiredFingers_)
    {
        ResetCurrentSession();
        return std::nullopt;
    }

    if (context.DurationMs() > MaxTapDurationMs || context.TotalDistance() > MovementThreshold)
    {
        ResetCurrentSession();
        return std::nullopt;
    }

    const Point center = context.Center();
    if (hasFirstTap_)
    {
        const double elapsedMs = CounterMs(context.CurrentTimestamp() - firstTapTimestamp_);
        if (elapsedMs < MinDoubleTapIntervalMs)
        {
            ResetCurrentSession();
            return std::nullopt;
        }

        if (elapsedMs <= MaxDoubleTapDelayMs && Distance(firstTapCenter_, center) <= DoubleTapDistance)
        {
            hasFirstTap_ = false;
            firstTapTimestamp_ = 0;
            firstTapCenter_ = {};
            ResetCurrentSession();
            return MakeResult(GestureType::ThreeFingerDoubleTap, GesturePhase::End, {}, context);
        }
    }

    hasFirstTap_ = true;
    firstTapTimestamp_ = context.CurrentTimestamp();
    firstTapCenter_ = center;
    ResetCurrentSession();
    return std::nullopt;
}

void MultiFingerTapRecognizer::ResetCurrentSession()
{
    currentTapInvalid_ = false;
}

void MultiFingerTapRecognizer::ResetAll()
{
    ResetCurrentSession();
    hasFirstTap_ = false;
    firstTapTimestamp_ = 0;
    firstTapCenter_ = {};
}

}  // namespace touchrev::gesture
