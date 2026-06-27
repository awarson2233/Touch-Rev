#pragma once

#include "input/gesture/GestureContext.h"

#include <vector>

namespace touchrev::gesture
{
struct GestureSessionConfig
{
    double stabilizationWindowMs = 70.0;
    double verticalStabilizationDelayMs = 80.0;
    double quickSwipeThreshold = 30.0;
    double threeFingerQuickSwipeThreshold = 50.0;
    double fourFingerQuickSwipeThreshold = 20.0;
    double fingerDropToleranceMs = 100.0;
    double maxClusterRadius = 480.0;
    double fourFingerRadiusMultiplier = 1.35;
    double fiveFingerRadiusMultiplier = 1.6;
    double maxRecognitionDurationMs = 800.0;
};

class GestureSession
{
public:
    static constexpr int kMinFingerCount = 3;
    static constexpr int kMaxFingerCount = 5;

    explicit GestureSession(std::int64_t startTimestamp);

    void AddContact(const GestureContact& contact);
    void UpdateContact(const GestureContact& contact);
    void RemoveContact(const GestureContact& contact);
    void SetCurrentTimestamp(std::int64_t timestamp);

    bool TryActivate();
    bool TryUpgradeFingerCount(int newCount);
    bool IsAllFingersUp() const;
    bool IsFingerDropToleranceExpired(int minimumFingerCount);
    bool IsRecognitionTimedOut() const;
    void Complete();
    void Cancel();

    GestureContext BuildContext(bool useRecognitionSnapshotWhenEmpty = false) const;
    std::vector<GestureContact> GetContacts() const { return contacts_; }
    std::vector<GestureContact> GetRecognitionContacts() const { return recognitionContacts_; }

    GestureSessionPhase Phase() const { return phase_; }
    int PeakFingerCount() const { return peakFingerCount_; }
    int LockedFingerCount() const { return lockedFingerCount_; }
    int CurrentFingerCount() const { return static_cast<int>(contacts_.size()); }
    Point Anchor() const { return anchor_; }
    std::int64_t StartTimestamp() const { return startTimestamp_; }
    std::int64_t CurrentTimestamp() const { return currentTimestamp_; }
    double DurationMs() const { return CounterMs(currentTimestamp_ - startTimestamp_); }

    bool GestureRecognized = false;
    GestureType ActiveGestureType = GestureType::None;

private:
    void OnFingerCountChanged();
    double GetTimeSinceLastFingerChange() const;
    double GetTotalMovement() const;
    Point CalculateCenter(const std::vector<GestureContact>& contacts) const;
    double CalculateClusterRadius(const std::vector<GestureContact>& contacts, Point center) const;
    bool IsSpatialClusterValid() const;
    double GetEffectiveMaxClusterRadius() const;
    const std::vector<GestureContact>& ContactsForSpatialValidation() const;
    void CaptureRecognitionContactsIfReady();

    GestureSessionConfig config_{};
    GestureSessionPhase phase_ = GestureSessionPhase::Pending;
    int peakFingerCount_ = 0;
    int lockedFingerCount_ = 0;
    std::int64_t startTimestamp_ = 0;
    std::int64_t currentTimestamp_ = 0;
    std::int64_t lastFingerCountChangeTimestamp_ = 0;
    std::int64_t fingerDropTimestamp_ = 0;
    int fingerCountBeforeDrop_ = 0;
    bool hasStartCenter_ = false;
    Point startCenter_{};
    Point anchor_{};
    std::vector<GestureContact> contacts_;
    std::vector<GestureContact> recognitionContacts_;
};

}  // namespace touchrev::gesture
