#include "input/gesture/ThreeFingerGestureRecognizer.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMaxFingerDistance = 420.0;
constexpr double kMaxDistanceSpreadRatio = 3.5;
constexpr double kLongPressThresholdMs = 500.0;
constexpr double kCandidateCancelMoveThreshold = 45.0;
constexpr double kMoveDirectionThreshold = 80.0;
constexpr double kTapMaxDurationMs = 280.0;
constexpr double kDoubleTapIntervalMs = 420.0;
constexpr double kDoubleTapDistance = 120.0;
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::ProcessFrame(const RawTouchInput::Frame& frame)
{
    UpdateActiveSnapshot(frame);
    std::array<FingerPoint, 3> fingers{};
    const bool threeFingerActive = TryExtractThreeFingers(fingers);
    const std::int64_t now = CounterNow();

    if (!threeFingerActive)
    {
        if (state_ == State::Candidate)
        {
            const Result result = FinishCandidateTap(lastCenter_, CalculateDistances(fingers));
            state_ = State::Idle;
            hasCandidateIds_ = false;
            return result;
        }

        if (state_ == State::LongPressActive)
        {
            state_ = State::Idle;
            hasCandidateIds_ = false;
            lastMoveDirection_ = Direction::None;
            return MakeResult(EventType::LongPressEnded, Direction::None, false, false, lastCenter_, {}, {});
        }

        return {};
    }

    const Distances distances = CalculateDistances(fingers);
    const bool sameHand = IsSameHand(distances);
    const Point center = Center(fingers);

    if (!sameHand)
    {
        if (state_ == State::LongPressActive)
        {
            state_ = State::Idle;
            hasCandidateIds_ = false;
            lastMoveDirection_ = Direction::None;
            return MakeResult(EventType::LongPressEnded, Direction::None, true, false, center, {}, distances);
        }

        state_ = State::Idle;
        hasCandidateIds_ = false;
        return MakeResult(EventType::None, Direction::None, true, false, center, {}, distances);
    }

    if (state_ == State::Idle)
    {
        state_ = State::Candidate;
        StoreCandidateIds(fingers);
        candidateStartQpc_ = now;
        candidateStartCenter_ = center;
        lastCenter_ = center;
        lastMoveDirection_ = Direction::None;
        return MakeResult(EventType::None, Direction::None, true, true, center, {}, distances);
    }

    if (!MatchesCandidateIds(fingers))
    {
        state_ = State::Candidate;
        StoreCandidateIds(fingers);
        candidateStartQpc_ = now;
        candidateStartCenter_ = center;
        lastCenter_ = center;
        lastMoveDirection_ = Direction::None;
        return MakeResult(EventType::None, Direction::None, true, true, center, {}, distances);
    }

    const Point deltaFromStart{center.x - candidateStartCenter_.x, center.y - candidateStartCenter_.y};
    const double distanceFromStart = std::hypot(deltaFromStart.x, deltaFromStart.y);

    if (state_ == State::Candidate)
    {
        if (distanceFromStart > kCandidateCancelMoveThreshold)
        {
            state_ = State::Idle;
            hasCandidateIds_ = false;
            return MakeResult(EventType::None, Direction::None, true, true, center, deltaFromStart, distances);
        }

        if (CounterMs(now - candidateStartQpc_) >= kLongPressThresholdMs)
        {
            state_ = State::LongPressActive;
            lastCenter_ = center;
            return MakeResult(EventType::LongPressStarted, Direction::None, true, true, center, deltaFromStart, distances);
        }

        lastCenter_ = center;
        return MakeResult(EventType::None, Direction::None, true, true, center, deltaFromStart, distances);
    }

    const Direction direction = ResolveDirection(deltaFromStart);
    const EventType type = direction != Direction::None && direction != lastMoveDirection_
                               ? EventType::LongPressMoved
                               : EventType::LongPressHolding;
    lastMoveDirection_ = direction != Direction::None ? direction : lastMoveDirection_;
    lastCenter_ = center;
    return MakeResult(type, direction, true, true, center, deltaFromStart, distances);
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::Tick()
{
    const std::int64_t now = CounterNow();
    if (state_ == State::Candidate && lastThreeFingerActive_ && lastSameHand_)
    {
        if (CounterMs(now - candidateStartQpc_) >= kLongPressThresholdMs)
        {
            state_ = State::LongPressActive;
            return MakeResult(EventType::LongPressStarted, Direction::None, true, true, lastCenter_, lastDelta_, lastDistances_);
        }
    }

    if (state_ == State::LongPressActive && lastThreeFingerActive_ && lastSameHand_)
    {
        return MakeResult(EventType::LongPressHolding, lastMoveDirection_, true, true, lastCenter_, lastDelta_, lastDistances_);
    }

    return {};
}

void ThreeFingerGestureRecognizer::Reset()
{
    state_ = State::Idle;
    candidateIds_ = {};
    hasCandidateIds_ = false;
    activeFingers_ = {};
    activeIds_ = {};
    candidateStartQpc_ = 0;
    candidateStartCenter_ = {};
    lastCenter_ = {};
    lastDelta_ = {};
    lastDistances_ = {};
    lastThreeFingerActive_ = false;
    lastSameHand_ = false;
    hasFirstTap_ = false;
    firstTapQpc_ = 0;
    firstTapCenter_ = {};
    lastMoveDirection_ = Direction::None;
}

std::int64_t ThreeFingerGestureRecognizer::CounterNow()
{
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

double ThreeFingerGestureRecognizer::CounterMs(std::int64_t delta)
{
    static const double frequency = []() {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();

    return static_cast<double>(delta) * 1000.0 / frequency;
}

double ThreeFingerGestureRecognizer::Distance(Point a, Point b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

ThreeFingerGestureRecognizer::Point ThreeFingerGestureRecognizer::Center(const std::array<FingerPoint, 3>& fingers)
{
    return {
        (fingers[0].point.x + fingers[1].point.x + fingers[2].point.x) / 3.0,
        (fingers[0].point.y + fingers[1].point.y + fingers[2].point.y) / 3.0,
    };
}

ThreeFingerGestureRecognizer::Distances ThreeFingerGestureRecognizer::CalculateDistances(const std::array<FingerPoint, 3>& fingers)
{
    Distances distances{};
    distances.d01 = Distance(fingers[0].point, fingers[1].point);
    distances.d02 = Distance(fingers[0].point, fingers[2].point);
    distances.d12 = Distance(fingers[1].point, fingers[2].point);
    distances.min = std::min({distances.d01, distances.d02, distances.d12});
    distances.max = std::max({distances.d01, distances.d02, distances.d12});
    distances.spreadRatio = distances.min > 0.0 ? distances.max / distances.min : 0.0;
    return distances;
}

bool ThreeFingerGestureRecognizer::IsSameHand(const Distances& distances)
{
    return distances.max <= kMaxFingerDistance &&
           distances.spreadRatio > 0.0 &&
           distances.spreadRatio <= kMaxDistanceSpreadRatio;
}

ThreeFingerGestureRecognizer::Direction ThreeFingerGestureRecognizer::ResolveDirection(Point delta)
{
    if (std::hypot(delta.x, delta.y) < kMoveDirectionThreshold)
    {
        return Direction::None;
    }

    if (std::abs(delta.x) > std::abs(delta.y))
    {
        return delta.x > 0.0 ? Direction::Right : Direction::Left;
    }

    return delta.y > 0.0 ? Direction::Down : Direction::Up;
}

void ThreeFingerGestureRecognizer::UpdateActiveSnapshot(const RawTouchInput::Frame& frame)
{
    for (const RawTouchInput::TouchPoint& point : frame.points)
    {
        if (point.contactId >= activeIds_.size())
        {
            continue;
        }

        if (!point.active || point.state == RawTouchInput::PointState::Released)
        {
            activeIds_[point.contactId] = false;
            activeFingers_[point.contactId] = {};
            continue;
        }

        activeIds_[point.contactId] = true;
        activeFingers_[point.contactId] = FingerPoint{
            .id = point.contactId,
            .point = {static_cast<double>(point.x), static_cast<double>(point.y)},
        };
    }
}

bool ThreeFingerGestureRecognizer::TryExtractThreeFingers(std::array<FingerPoint, 3>& fingers) const
{
    size_t count = 0;
    for (size_t id = 0; id < activeIds_.size(); ++id)
    {
        if (!activeIds_[id])
        {
            continue;
        }

        if (count >= fingers.size())
        {
            return false;
        }

        fingers[count++] = activeFingers_[id];
    }

    return count == fingers.size();
}

bool ThreeFingerGestureRecognizer::MatchesCandidateIds(const std::array<FingerPoint, 3>& fingers) const
{
    if (!hasCandidateIds_)
    {
        return false;
    }

    std::array<DWORD, 3> current{fingers[0].id, fingers[1].id, fingers[2].id};
    auto expected = candidateIds_;
    std::sort(current.begin(), current.end());
    std::sort(expected.begin(), expected.end());
    return current == expected;
}

void ThreeFingerGestureRecognizer::StoreCandidateIds(const std::array<FingerPoint, 3>& fingers)
{
    candidateIds_ = {fingers[0].id, fingers[1].id, fingers[2].id};
    hasCandidateIds_ = true;
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::MakeResult(
    EventType type,
    Direction direction,
    bool active,
    bool sameHand,
    Point center,
    Point delta,
    Distances distances)
{
    lastThreeFingerActive_ = active;
    lastSameHand_ = sameHand;
    lastCenter_ = center;
    lastDelta_ = delta;
    lastDistances_ = distances;

    return Result{
        .type = type,
        .direction = direction,
        .threeFingerActive = active,
        .sameHand = sameHand,
        .center = center,
        .delta = delta,
        .distances = distances,
    };
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::FinishCandidateTap(Point center, Distances distances)
{
    const std::int64_t now = CounterNow();
    const double durationMs = CounterMs(now - candidateStartQpc_);
    const double moveDistance = Distance(candidateStartCenter_, center);

    if (durationMs > kTapMaxDurationMs || moveDistance > kCandidateCancelMoveThreshold)
    {
        return MakeResult(EventType::None, Direction::None, false, true, center, {}, distances);
    }

    if (hasFirstTap_ &&
        CounterMs(now - firstTapQpc_) <= kDoubleTapIntervalMs &&
        Distance(firstTapCenter_, center) <= kDoubleTapDistance)
    {
        hasFirstTap_ = false;
        return MakeResult(EventType::DoubleTap, Direction::None, false, true, center, {}, distances);
    }

    hasFirstTap_ = true;
    firstTapQpc_ = now;
    firstTapCenter_ = center;
    return MakeResult(EventType::None, Direction::None, false, true, center, {}, distances);
}
