#include "input/gesture/ThreeFingerGestureRecognizer.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMaxFingerDistance = 4000.0;
constexpr double kLongPressThresholdMs = 500.0;
constexpr double kTapMoveThreshold = 45.0;
constexpr double kMoveEpsilon = 1.0;
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
            const Result result = FinishCandidateTap(lastCenter_, {});
            state_ = State::Idle;
            hasCandidateIds_ = false;
            return result;
        }

        if (state_ == State::LongPressActive)
        {
            state_ = State::Idle;
            hasCandidateIds_ = false;
            return MakeResult(EventType::LongPressEnded, false, false, lastCenter_, {}, {}, fingers);
        }

        return MakeResult(EventType::None, false, false, lastCenter_, {}, {}, fingers);
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
            return MakeResult(EventType::LongPressEnded, true, false, center, {}, distances, fingers);
        }

        state_ = State::Idle;
        hasCandidateIds_ = false;
        return MakeResult(EventType::None, true, false, center, {}, distances, fingers);
    }

    if (state_ == State::Idle)
    {
        state_ = State::Candidate;
        StoreCandidateIds(fingers);
        candidateStartQpc_ = now;
        candidateStartCenter_ = center;
        lastCenter_ = center;
        return MakeResult(EventType::None, true, true, center, {}, distances, fingers);
    }

    if (!MatchesCandidateIds(fingers))
    {
        state_ = State::Candidate;
        StoreCandidateIds(fingers);
        candidateStartQpc_ = now;
        candidateStartCenter_ = center;
        lastCenter_ = center;
        return MakeResult(EventType::None, true, true, center, {}, distances, fingers);
    }

    Point deltaFromStart{};
    if (!TryAverageDeltaById(candidateStartFingers_, fingers, deltaFromStart))
    {
        state_ = State::Candidate;
        StoreCandidateIds(fingers);
        candidateStartQpc_ = now;
        candidateStartCenter_ = center;
        lastCenter_ = center;
        return MakeResult(EventType::None, true, true, center, {}, distances, fingers);
    }

    if (state_ == State::Candidate)
    {
        if (CounterMs(now - candidateStartQpc_) >= kLongPressThresholdMs)
        {
            state_ = State::LongPressActive;
            lastCenter_ = center;
            return MakeResult(EventType::LongPressStarted, true, true, center, deltaFromStart, distances, fingers);
        }

        lastCenter_ = center;
        return MakeResult(EventType::None, true, true, center, deltaFromStart, distances, fingers);
    }

    const Point deltaChange{deltaFromStart.x - lastDelta_.x, deltaFromStart.y - lastDelta_.y};
    const EventType type = std::hypot(deltaChange.x, deltaChange.y) >= kMoveEpsilon
                               ? EventType::LongPressMoved
                               : EventType::LongPressHolding;
    lastCenter_ = center;
    return MakeResult(type, true, true, center, deltaFromStart, distances, fingers);
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::Tick()
{
    const std::int64_t now = CounterNow();
    std::array<FingerPoint, 3> fingers{};
    TryExtractThreeFingers(fingers);

    if (state_ == State::Candidate && lastThreeFingerActive_ && lastSameHand_)
    {
        if (CounterMs(now - candidateStartQpc_) >= kLongPressThresholdMs)
        {
            state_ = State::LongPressActive;
            return MakeResult(EventType::LongPressStarted, true, true, lastCenter_, lastDelta_, lastDistances_, fingers);
        }
    }

    if (state_ == State::LongPressActive && lastThreeFingerActive_ && lastSameHand_)
    {
        return MakeResult(EventType::LongPressHolding, true, true, lastCenter_, lastDelta_, lastDistances_, fingers);
    }

    return {};
}

void ThreeFingerGestureRecognizer::Reset()
{
    state_ = State::Idle;
    candidateIds_ = {};
    candidateStartFingers_ = {};
    hasCandidateIds_ = false;
    activeFingers_.clear();
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
    return distances.max <= kMaxFingerDistance;
}

bool ThreeFingerGestureRecognizer::TryAverageDeltaById(
    const std::array<FingerPoint, 3>& start,
    const std::array<FingerPoint, 3>& current,
    Point& delta)
{
    Point sum{};
    for (const FingerPoint& startFinger : start)
    {
        const auto currentFinger = std::find_if(current.begin(), current.end(), [id = startFinger.id](const FingerPoint& finger) {
            return finger.id == id;
        });
        if (currentFinger == current.end())
        {
            return false;
        }

        sum.x += currentFinger->point.x - startFinger.point.x;
        sum.y += currentFinger->point.y - startFinger.point.y;
    }

    delta = {sum.x / 3.0, sum.y / 3.0};
    return true;
}

void ThreeFingerGestureRecognizer::UpdateActiveSnapshot(const RawTouchInput::Frame& frame)
{
    if (frame.frameSync && frame.contactCount == 0)
    {
        activeFingers_.clear();
        return;
    }

    for (const RawTouchInput::TouchPoint& point : frame.points)
    {
        auto it = std::find_if(activeFingers_.begin(), activeFingers_.end(), [id = point.contactId](const FingerPoint& fp) {
            return fp.id == id;
        });

        if (!point.active || point.state == RawTouchInput::PointState::Released)
        {
            if (it != activeFingers_.end())
            {
                activeFingers_.erase(it);
            }
        }
        else
        {
            FingerPoint fp{
                .id = point.contactId,
                .point = {static_cast<double>(point.x), static_cast<double>(point.y)},
            };
            if (it != activeFingers_.end())
            {
                *it = fp;
            }
            else
            {
                activeFingers_.push_back(fp);
            }
        }
    }

    std::sort(activeFingers_.begin(), activeFingers_.end(), [](const FingerPoint& a, const FingerPoint& b) {
        return a.id < b.id;
    });
}

bool ThreeFingerGestureRecognizer::TryExtractThreeFingers(std::array<FingerPoint, 3>& fingers) const
{
    if (activeFingers_.size() != 3)
    {
        return false;
    }

    fingers = {activeFingers_[0], activeFingers_[1], activeFingers_[2]};
    return true;
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
    candidateStartFingers_ = fingers;
    hasCandidateIds_ = true;
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::MakeResult(
    EventType type,
    bool active,
    bool sameHand,
    Point center,
    Point delta,
    Distances distances,
    const std::array<FingerPoint, 3>& fingers)
{
    lastThreeFingerActive_ = active;
    lastSameHand_ = sameHand;
    lastCenter_ = center;
    lastDelta_ = delta;
    lastDistances_ = distances;

    std::array<DWORD, 3> activeIds{};
    if (active)
    {
        activeIds = {fingers[0].id, fingers[1].id, fingers[2].id};
    }

    return Result{
        .type = type,
        .threeFingerActive = active,
        .sameHand = sameHand,
        .center = center,
        .delta = delta,
        .distances = distances,
        .activeIds = activeIds,
        .startIds = candidateIds_,
        .deltaValid = hasCandidateIds_,
    };
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::FinishCandidateTap(Point center, Distances distances)
{
    const std::int64_t now = CounterNow();
    const double durationMs = CounterMs(now - candidateStartQpc_);
    const double moveDistance = Distance(candidateStartCenter_, center);

    std::array<FingerPoint, 3> fingers{};
    TryExtractThreeFingers(fingers);

    if (durationMs > kTapMaxDurationMs || moveDistance > kTapMoveThreshold)
    {
        return MakeResult(EventType::None, false, true, center, {}, distances, fingers);
    }

    if (hasFirstTap_ &&
        CounterMs(now - firstTapQpc_) <= kDoubleTapIntervalMs &&
        Distance(firstTapCenter_, center) <= kDoubleTapDistance)
    {
        hasFirstTap_ = false;
        return MakeResult(EventType::DoubleTap, false, true, center, {}, distances, fingers);
    }

    hasFirstTap_ = true;
    firstTapQpc_ = now;
    firstTapCenter_ = center;
    return MakeResult(EventType::None, false, true, center, {}, distances, fingers);
}
