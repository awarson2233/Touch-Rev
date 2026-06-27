#include "input/gesture/GestureSession.h"

#include <algorithm>

namespace touchrev::gesture
{
GestureSession::GestureSession(std::int64_t startTimestamp)
    : startTimestamp_(startTimestamp),
      currentTimestamp_(startTimestamp),
      lastFingerCountChangeTimestamp_(startTimestamp)
{
}

void GestureSession::AddContact(const GestureContact& contact)
{
    currentTimestamp_ = std::max(currentTimestamp_, contact.timestamp);
    const auto existing = std::find_if(contacts_.begin(), contacts_.end(), [id = contact.contactId](const GestureContact& item) {
        return item.contactId == id;
    });

    if (existing == contacts_.end())
    {
        contacts_.push_back(contact);
        OnFingerCountChanged();
    }
    else
    {
        *existing = contact;
        CaptureRecognitionContactsIfReady();
    }
}

void GestureSession::UpdateContact(const GestureContact& contact)
{
    currentTimestamp_ = std::max(currentTimestamp_, contact.timestamp);
    const auto existing = std::find_if(contacts_.begin(), contacts_.end(), [id = contact.contactId](const GestureContact& item) {
        return item.contactId == id;
    });

    if (existing != contacts_.end())
    {
        *existing = contact;
        CaptureRecognitionContactsIfReady();
    }
    else
    {
        contacts_.push_back(contact);
        OnFingerCountChanged();
    }
}

void GestureSession::RemoveContact(const GestureContact& contact)
{
    currentTimestamp_ = std::max(currentTimestamp_, contact.timestamp);
    const auto existing = std::find_if(contacts_.begin(), contacts_.end(), [id = contact.contactId](const GestureContact& item) {
        return item.contactId == id;
    });

    if (existing == contacts_.end())
    {
        return;
    }

    *existing = contact;
    CaptureRecognitionContactsIfReady();
    contacts_.erase(existing);
    if (phase_ == GestureSessionPhase::Active && !contacts_.empty())
    {
        if (fingerDropTimestamp_ == 0)
        {
            fingerDropTimestamp_ = contact.timestamp;
            fingerCountBeforeDrop_ = static_cast<int>(contacts_.size()) + 1;
        }
    }
    else
    {
        OnFingerCountChanged();
    }
}

void GestureSession::SetCurrentTimestamp(std::int64_t timestamp)
{
    currentTimestamp_ = std::max(currentTimestamp_, timestamp);
}

bool GestureSession::TryActivate()
{
    if (phase_ != GestureSessionPhase::Pending)
    {
        return phase_ == GestureSessionPhase::Active;
    }

    if (peakFingerCount_ < kMinFingerCount)
    {
        return false;
    }

    bool shouldActivate = false;
    if (IsAllFingersUp())
    {
        shouldActivate = true;
    }

    double requiredStabilization = config_.stabilizationWindowMs;
    if (peakFingerCount_ == 3)
    {
        const std::vector<GestureContact>& contacts = ContactsForSpatialValidation();
        const Point currentCenter = CalculateCenter(contacts);
        const Point origin = hasStartCenter_ ? startCenter_ : currentCenter;
        const double deltaX = std::abs(currentCenter.x - origin.x);
        const double deltaY = std::abs(currentCenter.y - origin.y);
        if (deltaY > deltaX * 1.3)
        {
            requiredStabilization += config_.verticalStabilizationDelayMs;
        }
    }

    if (!shouldActivate && GetTimeSinceLastFingerChange() >= requiredStabilization)
    {
        shouldActivate = true;
    }

    if (peakFingerCount_ >= kMaxFingerCount)
    {
        shouldActivate = true;
    }

    const double effectiveQuickThreshold = peakFingerCount_ == 3
                                               ? config_.threeFingerQuickSwipeThreshold
                                               : (peakFingerCount_ == 4 ? config_.fourFingerQuickSwipeThreshold : config_.quickSwipeThreshold);
    if (GetTotalMovement() >= effectiveQuickThreshold)
    {
        shouldActivate = true;
    }

    if (!shouldActivate && peakFingerCount_ >= kMinFingerCount && DurationMs() > 15.0 && GetTotalMovement() < 5.0)
    {
        shouldActivate = true;
    }

    if (shouldActivate && !IsSpatialClusterValid())
    {
        shouldActivate = false;
    }

    if (!shouldActivate)
    {
        return false;
    }

    phase_ = GestureSessionPhase::Active;
    lockedFingerCount_ = peakFingerCount_;
    ActiveGestureType = GestureType::None;
    anchor_ = CalculateCenter(ContactsForSpatialValidation());
    return true;
}

bool GestureSession::TryUpgradeFingerCount(int newCount)
{
    if (phase_ != GestureSessionPhase::Active || GestureRecognized)
    {
        return false;
    }

    if (newCount > lockedFingerCount_ && newCount <= kMaxFingerCount)
    {
        lockedFingerCount_ = newCount;
        peakFingerCount_ = std::max(peakFingerCount_, newCount);
        return true;
    }

    return false;
}

bool GestureSession::IsAllFingersUp() const
{
    return contacts_.empty();
}

bool GestureSession::IsFingerDropToleranceExpired(int minimumFingerCount)
{
    const int minCount = std::max(0, minimumFingerCount);
    const int currentCount = CurrentFingerCount();
    if (currentCount >= minCount)
    {
        fingerDropTimestamp_ = 0;
        fingerCountBeforeDrop_ = 0;
        return false;
    }

    if (fingerDropTimestamp_ == 0)
    {
        fingerDropTimestamp_ = currentTimestamp_;
        fingerCountBeforeDrop_ = currentCount;
        return false;
    }

    return CounterMs(currentTimestamp_ - fingerDropTimestamp_) >= config_.fingerDropToleranceMs;
}

bool GestureSession::IsRecognitionTimedOut() const
{
    return DurationMs() > config_.maxRecognitionDurationMs;
}

void GestureSession::Complete()
{
    phase_ = GestureSessionPhase::Completed;
}

void GestureSession::Cancel()
{
    phase_ = GestureSessionPhase::Cancelled;
}

GestureContext GestureSession::BuildContext(bool useRecognitionSnapshotWhenEmpty) const
{
    const std::vector<GestureContact>& contacts = (useRecognitionSnapshotWhenEmpty && contacts_.empty() && !recognitionContacts_.empty())
                                                      ? recognitionContacts_
                                                      : contacts_;
    return GestureContext(contacts, startTimestamp_, currentTimestamp_, anchor_);
}

void GestureSession::OnFingerCountChanged()
{
    const int count = CurrentFingerCount();
    if (count > peakFingerCount_)
    {
        peakFingerCount_ = count;
        lastFingerCountChangeTimestamp_ = currentTimestamp_;
    }

    if (!hasStartCenter_ && count >= kMinFingerCount)
    {
        startCenter_ = CalculateCenter(contacts_);
        hasStartCenter_ = true;
    }

    CaptureRecognitionContactsIfReady();
    fingerDropTimestamp_ = 0;
    fingerCountBeforeDrop_ = 0;
}

double GestureSession::GetTimeSinceLastFingerChange() const
{
    return CounterMs(currentTimestamp_ - lastFingerCountChangeTimestamp_);
}

double GestureSession::GetTotalMovement() const
{
    const std::vector<GestureContact>& contacts = ContactsForSpatialValidation();
    if (contacts.empty())
    {
        return 0.0;
    }

    double sumDistance = 0.0;
    for (const GestureContact& contact : contacts)
    {
        sumDistance += contact.DistanceFromStart();
    }

    return sumDistance / static_cast<double>(contacts.size());
}

Point GestureSession::CalculateCenter(const std::vector<GestureContact>& contacts) const
{
    if (contacts.empty())
    {
        return {};
    }

    Point sum{};
    for (const GestureContact& contact : contacts)
    {
        sum.x += contact.current.x;
        sum.y += contact.current.y;
    }

    const double count = static_cast<double>(contacts.size());
    return {sum.x / count, sum.y / count};
}

double GestureSession::CalculateClusterRadius(const std::vector<GestureContact>& contacts, Point center) const
{
    double maxDistance = 0.0;
    for (const GestureContact& contact : contacts)
    {
        maxDistance = std::max(maxDistance, Distance(contact.current, center));
    }
    return maxDistance;
}

bool GestureSession::IsSpatialClusterValid() const
{
    const std::vector<GestureContact>& contacts = ContactsForSpatialValidation();
    if (contacts.size() < kMinFingerCount)
    {
        return false;
    }

    const Point center = CalculateCenter(contacts);
    return CalculateClusterRadius(contacts, center) <= GetEffectiveMaxClusterRadius();
}

double GestureSession::GetEffectiveMaxClusterRadius() const
{
    if (peakFingerCount_ >= 5)
    {
        return config_.maxClusterRadius * config_.fiveFingerRadiusMultiplier;
    }
    if (peakFingerCount_ == 4)
    {
        return config_.maxClusterRadius * config_.fourFingerRadiusMultiplier;
    }
    return config_.maxClusterRadius;
}

const std::vector<GestureContact>& GestureSession::ContactsForSpatialValidation() const
{
    if (contacts_.empty() && !recognitionContacts_.empty())
    {
        return recognitionContacts_;
    }
    return contacts_;
}

void GestureSession::CaptureRecognitionContactsIfReady()
{
    if (contacts_.size() >= kMinFingerCount)
    {
        recognitionContacts_ = contacts_;
    }
}

}  // namespace touchrev::gesture
