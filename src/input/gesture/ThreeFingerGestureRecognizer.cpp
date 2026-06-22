#include "input/gesture/ThreeFingerGestureRecognizer.h"

#include "common/Win32Error.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace
{
constexpr double kMaxFingerDistance = 800.0;
constexpr double kLongPressThresholdMs = 400.0;
constexpr double kTapMoveThreshold = 30.0;
constexpr double kMoveEpsilon = 0.1;
constexpr double kTapMaxDurationMs = 400.0;
constexpr double kMinDoubleTapIntervalMs = 100.0;
constexpr double kDoubleTapIntervalMs = 450.0;
constexpr double kDoubleTapDistance = 150.0;
constexpr double kMaxStartMovement = 40.0;
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::ProcessFrame(const RawTouchInput::Frame& frame)
{
    UpdateActiveSnapshot(frame);
    std::array<FingerPoint, 3> fingers{};
    const bool threeFingerActive = TryExtractThreeFingers(fingers);
    const std::int64_t now = CounterNow();

    // 缓存原始触摸坐标（用于 Tick 触发时计算触摸中心）
    if (threeFingerActive)
    {
        lastRawCenterX_ = static_cast<LONG>((fingers[0].point.x + fingers[1].point.x + fingers[2].point.x) / 3.0 * 10.0);
        lastRawCenterY_ = static_cast<LONG>((fingers[0].point.y + fingers[1].point.y + fingers[2].point.y) / 3.0 * 10.0);
        hasLastRawCenter_ = true;
    }

    if (!threeFingerActive)
    {
        if (state_ == State::Candidate)
        {
            const Result result = FinishCandidateTap(lastCenter_, lastDistances_, lastFingers_);
            state_ = State::Idle;
            hasCandidateIds_ = false;
            return result;
        }

        if (state_ == State::LongPressActive)
        {
            // 降级容错：检查是否是临时 ID 断开（仍有 2+ 个原始 ID 的手指）
            if (PartiallyMatchesCandidateIds() && activeFingers_.size() >= 2)
            {
                // 使用剩余手指的中心继续跟踪（降级模式）
                Point center = CalculateCenterFromActive();

                // 更新缓存的原始坐标
                lastRawCenterX_ = 0;
                lastRawCenterY_ = 0;
                for (const auto& finger : activeFingers_)
                {
                    lastRawCenterX_ += static_cast<LONG>(finger.point.x * 10.0);
                    lastRawCenterY_ += static_cast<LONG>(finger.point.y * 10.0);
                }
                lastRawCenterX_ /= static_cast<LONG>(activeFingers_.size());
                lastRawCenterY_ /= static_cast<LONG>(activeFingers_.size());
                hasLastRawCenter_ = true;

                const Point frameDelta{center.x - lastCenter_.x, center.y - lastCenter_.y};
                const EventType type = std::hypot(frameDelta.x, frameDelta.y) >= kMoveEpsilon
                                           ? EventType::LongPressMoved
                                           : EventType::LongPressHolding;
                lastCenter_ = center;

                std::wstringstream log;
                log << L"[Gesture] Degraded mode: tracking with " << activeFingers_.size() << L" fingers";
                DebugLog(log.str());

                // 构造虚拟的 3 指数组（用于兼容 MakeResult）
                std::array<FingerPoint, 3> degradedFingers{};
                for (size_t i = 0; i < std::min(activeFingers_.size(), size_t(3)); ++i)
                {
                    degradedFingers[i] = activeFingers_[i];
                }

                return MakeResult(type, true, true, center, frameDelta, {}, degradedFingers);
            }

            // 真正终止：手指完全消失或出现不匹配的新 ID
            std::wstringstream log;
            log << L"[Gesture] LongPress ended: finger count=" << activeFingers_.size()
                << L" partialMatch=" << (PartiallyMatchesCandidateIds() ? L"true" : L"false");
            DebugLog(log.str());

            state_ = State::Idle;
            hasCandidateIds_ = false;
            return MakeResult(EventType::LongPressEnded, false, false, lastCenter_, {}, {}, fingers);
        }

        state_ = State::Idle;
        hasCandidateIds_ = false;
        return MakeResult(EventType::None, false, false, lastCenter_, {}, {}, fingers);
    }

    const Distances distances = CalculateDistances(fingers);
    const bool sameHand = IsSameHand(distances);
    lastFingers_ = fingers;
    const Point center = Center(fingers);

    if (!sameHand)
    {
        if (state_ == State::LongPressActive)
        {
            std::wstringstream log;
            log << L"[Gesture] LongPress ended: hand spread too wide. Max distance: " << distances.max << L" (limit: 800)";
            DebugLog(log.str());

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
        std::wstringstream log;
        log << L"[Gesture] ID mismatch, resetting state. Current IDs: "
            << fingers[0].id << L", " << fingers[1].id << L", " << fingers[2].id;
        if (hasCandidateIds_)
        {
            log << L" Expected: " << candidateIds_[0] << L", " << candidateIds_[1] << L", " << candidateIds_[2];
        }
        log << L" State: " << static_cast<int>(state_);
        DebugLog(log.str());

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

    if (state_ == State::Tracking)
    {
        lastCenter_ = center;
        return MakeResult(EventType::None, true, true, center, {}, distances, fingers);
    }

    if (state_ == State::Candidate)
    {
        const double distFromAnchor = Distance(candidateStartCenter_, center);
        if (distFromAnchor > kMaxStartMovement * 2.4)
        {
            state_ = State::Tracking;
            lastCenter_ = center;
            return MakeResult(EventType::None, true, true, center, {}, distances, fingers);
        }

        if (CounterMs(now - candidateStartQpc_) >= kLongPressThresholdMs)
        {
            if (distFromAnchor <= kMaxStartMovement * 1.5)
            {
                DebugLog(L"[Gesture] LongPressStarted triggered");
                state_ = State::LongPressActive;
                lastCenter_ = center;
                return MakeResult(EventType::LongPressStarted, true, true, center, {0.0, 0.0}, distances, fingers);
            }
            else
            {
                std::wstringstream log;
                log << L"[Gesture] Movement too large for long press: " << distFromAnchor << L" (limit: " << (kMaxStartMovement * 1.5) << L")";
                DebugLog(log.str());

                state_ = State::Tracking;
                lastCenter_ = center;
                return MakeResult(EventType::None, true, true, center, {}, distances, fingers);
            }
        }

        lastCenter_ = center;
        return MakeResult(EventType::None, true, true, center, {}, distances, fingers);
    }

    if (state_ == State::LongPressActive)
    {
        const Point frameDelta{center.x - lastCenter_.x, center.y - lastCenter_.y};
        const EventType type = std::hypot(frameDelta.x, frameDelta.y) >= kMoveEpsilon
                                   ? EventType::LongPressMoved
                                   : EventType::LongPressHolding;
        lastCenter_ = center;
        return MakeResult(type, true, true, center, frameDelta, distances, fingers);
    }

    return {};
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::Tick()
{
    const std::int64_t now = CounterNow();
    std::array<FingerPoint, 3> fingers{};
    TryExtractThreeFingers(fingers);

    if (state_ == State::Candidate && lastThreeFingerActive_ && lastSameHand_)
    {
        const double distFromAnchor = Distance(candidateStartCenter_, lastCenter_);
        if (distFromAnchor > kMaxStartMovement * 2.4)
        {
            state_ = State::Tracking;
        }
        else if (CounterMs(now - candidateStartQpc_) >= kLongPressThresholdMs)
        {
            if (distFromAnchor <= kMaxStartMovement * 1.5)
            {
                state_ = State::LongPressActive;
                return MakeResult(EventType::LongPressStarted, true, true, lastCenter_, {0.0, 0.0}, lastDistances_, fingers);
            }
            else
            {
                state_ = State::Tracking;
            }
        }
    }

    if (state_ == State::LongPressActive && lastThreeFingerActive_ && lastSameHand_)
    {
        return MakeResult(EventType::LongPressHolding, true, true, lastCenter_, {0.0, 0.0}, lastDistances_, fingers);
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
    lastFingers_ = {};
    lastThreeFingerActive_ = false;
    lastSameHand_ = false;
    lastRawCenterX_ = 0;
    lastRawCenterY_ = 0;
    hasLastRawCenter_ = false;
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
                .point = {static_cast<double>(point.x) * 0.1, static_cast<double>(point.y) * 0.1},
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

    // 精确匹配
    if (current == expected)
    {
        return true;
    }

    // 容错模式：允许最多 1 个 ID 变化（应对硬件报告不稳定）
    // 统计不匹配的 ID 数量
    int mismatchCount = 0;
    for (size_t i = 0; i < 3; ++i)
    {
        if (std::find(expected.begin(), expected.end(), current[i]) == expected.end())
        {
            ++mismatchCount;
        }
    }

    // 如果只有 1 个 ID 变化，且处于长按状态，则容忍并继续跟踪
    if (mismatchCount <= 1 && state_ == State::LongPressActive)
    {
        std::wstringstream log;
        log << L"[Gesture] ID tolerance: mismatchCount=" << mismatchCount
            << L" current=[" << current[0] << L"," << current[1] << L"," << current[2] << L"]"
            << L" expected=[" << expected[0] << L"," << expected[1] << L"," << expected[2] << L"]";
        DebugLog(log.str());
        return true;
    }

    return false;
}

bool ThreeFingerGestureRecognizer::PartiallyMatchesCandidateIds() const
{
    if (!hasCandidateIds_ || activeFingers_.empty())
    {
        return false;
    }

    // 至少需要 2 个手指
    if (activeFingers_.size() < 2)
    {
        return false;
    }

    // 所有当前活动的手指 ID 都必须在原始候选 ID 集合中
    for (const auto& finger : activeFingers_)
    {
        if (std::find(candidateIds_.begin(), candidateIds_.end(), finger.id) == candidateIds_.end())
        {
            return false;  // 发现不属于原始 ID 的新手指
        }
    }

    return true;
}

ThreeFingerGestureRecognizer::Point ThreeFingerGestureRecognizer::CalculateCenterFromActive() const
{
    if (activeFingers_.empty())
    {
        return {};
    }

    double sumX = 0.0;
    double sumY = 0.0;
    for (const auto& finger : activeFingers_)
    {
        sumX += finger.point.x;
        sumY += finger.point.y;
    }

    return {sumX / static_cast<double>(activeFingers_.size()),
            sumY / static_cast<double>(activeFingers_.size())};
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
        .rawCenterX = lastRawCenterX_,
        .rawCenterY = lastRawCenterY_,
        .hasRawCenter = hasLastRawCenter_,
    };
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::FinishCandidateTap(
    Point center,
    Distances distances,
    const std::array<FingerPoint, 3>& fingers)
{
    const std::int64_t now = CounterNow();
    const double durationMs = CounterMs(now - candidateStartQpc_);
    const double moveDistance = Distance(candidateStartCenter_, center);

    if (durationMs > kTapMaxDurationMs || moveDistance > kTapMoveThreshold)
    {
        return MakeResult(EventType::None, false, true, center, {}, distances, fingers);
    }

    if (hasFirstTap_)
    {
        const double elapsedMs = CounterMs(now - firstTapQpc_);
        if (elapsedMs < kMinDoubleTapIntervalMs)
        {
            // 时间间隔过短，视为硬件抖动噪声，直接忽略，不触发双击且不重置第一击状态
            return MakeResult(EventType::None, false, true, center, {}, distances, fingers);
        }

        if (elapsedMs <= kDoubleTapIntervalMs &&
            Distance(firstTapCenter_, center) <= kDoubleTapDistance)
        {
            hasFirstTap_ = false;
            return MakeResult(EventType::DoubleTap, false, true, center, {}, distances, fingers);
        }
    }

    hasFirstTap_ = true;
    firstTapQpc_ = now;
    firstTapCenter_ = center;
    return MakeResult(EventType::None, false, true, center, {}, distances, fingers);
}
