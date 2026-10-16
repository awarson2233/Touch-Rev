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
                RebaseContacts(context);
                virtualCenter_ = context.Center();
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

        // 虚拟光标只由各指自身的位移推动：手指抬起只是不再贡献位移，光标不会因几何中心变化而跳动。
        const Point delta = ConsumeContactDelta(context);
        virtualCenter_.x += delta.x;
        virtualCenter_.y += delta.y;
        GestureRecognitionResult result = MakeResult(GestureType::ThreeFingerLongPress, GesturePhase::Update, delta, context);
        result.center = virtualCenter_;
        return result;
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
    triggerStartTimestamp_ = 0;
    trackedPositions_.clear();
    virtualCenter_ = {};
}

Point ThreeFingerLongPressRecognizer::ConsumeContactDelta(const GestureContext& context)
{
    // 每个接触事件只有一根手指移动，按当前手指数取平均，一帧内累加后即为整只手的平均位移。
    Point sum{};
    bool matched = false;
    std::vector<TrackedPosition> next;
    next.reserve(context.Contacts().size());
    for (const GestureContact& contact : context.Contacts())
    {
        const auto previous = std::find_if(trackedPositions_.begin(), trackedPositions_.end(), [id = contact.contactId](const TrackedPosition& item) {
            return item.contactId == id;
        });
        if (previous != trackedPositions_.end())
        {
            sum.x += contact.current.x - previous->position.x;
            sum.y += contact.current.y - previous->position.y;
            matched = true;
        }
        next.push_back({contact.contactId, contact.current});
    }
    trackedPositions_ = std::move(next);

    if (!matched)
    {
        return {};
    }
    const double count = static_cast<double>(context.FingerCount());
    return {sum.x / count, sum.y / count};
}

void ThreeFingerLongPressRecognizer::RebaseContacts(const GestureContext& context)
{
    trackedPositions_.clear();
    for (const GestureContact& contact : context.Contacts())
    {
        trackedPositions_.push_back({contact.contactId, contact.current});
    }
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
