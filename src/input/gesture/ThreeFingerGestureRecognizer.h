#pragma once

#include "input/raw/RawTouchInput.h"

#include <array>
#include <cstdint>
#include <vector>

class ThreeFingerGestureRecognizer
{
public:
    enum class EventType
    {
        None,
        LongPressStarted,
        LongPressHolding,
        LongPressMoved,
        LongPressEnded,
        DoubleTap,
    };

    struct Point
    {
        double x = 0.0;
        double y = 0.0;
    };

    struct Distances
    {
        double d01 = 0.0;
        double d02 = 0.0;
        double d12 = 0.0;
        double min = 0.0;
        double max = 0.0;
        double spreadRatio = 0.0;
    };

    struct Result
    {
        EventType type = EventType::None;
        bool threeFingerActive = false;
        bool sameHand = false;
        Point center{};
        Point delta{};
        Distances distances{};
        std::array<DWORD, 3> activeIds{};
        std::array<DWORD, 3> startIds{};
        bool deltaValid = false;
        // 触摸中心的原始屏幕坐标（0.1mm 单位），用于显示器定位
        LONG rawCenterX = 0;
        LONG rawCenterY = 0;
        bool hasRawCenter = false;
    };

    Result ProcessFrame(const RawTouchInput::Frame& frame);
    Result Tick();
    void Reset();

private:
    enum class State
    {
        Idle,
        Candidate,
        Tracking,
        LongPressActive,
    };

    struct FingerPoint
    {
        DWORD id = 0;
        Point point{};
    };

    static std::int64_t CounterNow();
    static double CounterMs(std::int64_t delta);
    static double Distance(Point a, Point b);
    static Point Center(const std::array<FingerPoint, 3>& fingers);
    static Distances CalculateDistances(const std::array<FingerPoint, 3>& fingers);
    static bool IsSameHand(const Distances& distances);
    static bool TryAverageDeltaById(const std::array<FingerPoint, 3>& start, const std::array<FingerPoint, 3>& current, Point& delta);

    void UpdateActiveSnapshot(const RawTouchInput::Frame& frame);
    bool TryExtractThreeFingers(std::array<FingerPoint, 3>& fingers) const;
    bool MatchesCandidateIds(const std::array<FingerPoint, 3>& fingers) const;
    bool PartiallyMatchesCandidateIds() const;
    Point CalculateCenterFromActive() const;
    void StoreCandidateIds(const std::array<FingerPoint, 3>& fingers);
    Result MakeResult(EventType type, bool active, bool sameHand, Point center, Point delta, Distances distances, const std::array<FingerPoint, 3>& fingers);
    Result FinishCandidateTap(Point center, Distances distances, const std::array<FingerPoint, 3>& fingers);

    State state_ = State::Idle;
    std::vector<FingerPoint> activeFingers_;
    std::array<DWORD, 3> candidateIds_{};
    std::array<FingerPoint, 3> candidateStartFingers_{};
    bool hasCandidateIds_ = false;
    std::int64_t candidateStartQpc_ = 0;
    Point candidateStartCenter_{};
    Point lastCenter_{};
    Point lastDelta_{};
    Distances lastDistances_{};
    std::array<FingerPoint, 3> lastFingers_{};
    bool lastThreeFingerActive_ = false;
    bool lastSameHand_ = false;
    // 缓存最后有效的原始触摸坐标（用于 Tick 触发时计算触摸中心）
    LONG lastRawCenterX_ = 0;
    LONG lastRawCenterY_ = 0;
    bool hasLastRawCenter_ = false;

    bool hasFirstTap_ = false;
    std::int64_t firstTapQpc_ = 0;
    Point firstTapCenter_{};
};
