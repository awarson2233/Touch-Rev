#pragma once

#include "input/gesture/GestureContact.h"
#include "input/gesture/GestureRecognizers.h"
#include "input/gesture/GestureSession.h"
#include "input/raw/RawTouchInput.h"

#include <array>
#include <memory>
#include <optional>
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
    struct FingerPoint
    {
        DWORD id = 0;
        Point point{};
        LONG rawX = 0;
        LONG rawY = 0;
    };

    static double Distance(Point a, Point b);
    static Distances CalculateDistances(const std::array<FingerPoint, 3>& fingers);
    static bool IsSameHand(const Distances& distances);

    std::optional<touchrev::gesture::GestureRecognitionResult> ProcessContactEvent(const touchrev::gesture::GestureContactEvent& event);
    std::optional<touchrev::gesture::GestureRecognitionResult> HandlePendingPhase();
    std::optional<touchrev::gesture::GestureRecognitionResult> HandleActivePhase();
    std::optional<touchrev::gesture::GestureRecognitionResult> FinalizeSession(touchrev::gesture::GestureEndReason reason);
    void BeginRecognizers();
    void ResetRecognizersForSession();
    int MinimumFingerQuorum() const;

    Result ConvertResult(const touchrev::gesture::GestureRecognitionResult& result);
    Result MakeResult(
        EventType type,
        bool active,
        bool sameHand,
        Point center,
        Point delta,
        Distances distances,
        const std::array<FingerPoint, 3>& fingers);
    static Point ToPublicPoint(touchrev::gesture::Point point);
    static std::array<FingerPoint, 3> ExtractFingerPoints(const std::vector<touchrev::gesture::GestureContact>& contacts);
    void CaptureSessionStartIds(const std::vector<touchrev::gesture::GestureContact>& contacts);
    void UpdateRawCenterCache(const std::vector<touchrev::gesture::GestureContact>& contacts);

    touchrev::gesture::GestureContactTracker contactTracker_;
    std::unique_ptr<touchrev::gesture::GestureSession> session_;
    touchrev::gesture::ThreeFingerLongPressRecognizer longPressRecognizer_;
    touchrev::gesture::MultiFingerTapRecognizer tapRecognizer_{3};
    bool recognizersBegun_ = false;
    std::array<DWORD, 3> sessionStartIds_{};
    bool hasSessionStartIds_ = false;

    Point lastCenter_{};
    Point lastDelta_{};
    Distances lastDistances_{};
    bool lastThreeFingerActive_ = false;
    bool lastSameHand_ = false;
    LONG lastRawCenterX_ = 0;
    LONG lastRawCenterY_ = 0;
    bool hasLastRawCenter_ = false;
};
