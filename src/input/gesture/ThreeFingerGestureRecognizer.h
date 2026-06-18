#pragma once

#include "input/raw/RawTouchInput.h"

#include <array>
#include <cstdint>

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

    enum class Direction
    {
        None,
        Up,
        Down,
        Left,
        Right,
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
        Direction direction = Direction::None;
        bool threeFingerActive = false;
        bool sameHand = false;
        Point center{};
        Point delta{};
        Distances distances{};
    };

    Result ProcessFrame(const RawTouchInput::Frame& frame);
    Result Tick();
    void Reset();

private:
    enum class State
    {
        Idle,
        Candidate,
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
    static Direction ResolveDirection(Point delta);

    void UpdateActiveSnapshot(const RawTouchInput::Frame& frame);
    bool TryExtractThreeFingers(std::array<FingerPoint, 3>& fingers) const;
    bool MatchesCandidateIds(const std::array<FingerPoint, 3>& fingers) const;
    void StoreCandidateIds(const std::array<FingerPoint, 3>& fingers);
    Result MakeResult(EventType type, Direction direction, bool active, bool sameHand, Point center, Point delta, Distances distances);
    Result FinishCandidateTap(Point center, Distances distances);

    State state_ = State::Idle;
    std::array<FingerPoint, 256> activeFingers_{};
    std::array<bool, 256> activeIds_{};
    std::array<DWORD, 3> candidateIds_{};
    bool hasCandidateIds_ = false;
    std::int64_t candidateStartQpc_ = 0;
    Point candidateStartCenter_{};
    Point lastCenter_{};
    Point lastDelta_{};
    Distances lastDistances_{};
    bool lastThreeFingerActive_ = false;
    bool lastSameHand_ = false;

    bool hasFirstTap_ = false;
    std::int64_t firstTapQpc_ = 0;
    Point firstTapCenter_{};

    Direction lastMoveDirection_ = Direction::None;
};
