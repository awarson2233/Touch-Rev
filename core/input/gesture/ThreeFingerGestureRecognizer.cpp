#include "input/gesture/ThreeFingerGestureRecognizer.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMaxFingerDistance = 800.0;
constexpr double kMoveEpsilon = 0.1;
constexpr double kFastTrackTapMaxDurationMs = 500.0;
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::ProcessFrame(const RawTouchInput::Frame& frame)
{
    const std::vector<touchrev::gesture::GestureContactEvent> events = contactTracker_.ProcessFrame(frame);
    std::optional<touchrev::gesture::GestureRecognitionResult> selectedResult;
    std::optional<touchrev::gesture::GestureRecognitionResult> accumulatedMoveResult;

    for (const touchrev::gesture::GestureContactEvent& event : events)
    {
        std::optional<touchrev::gesture::GestureRecognitionResult> result = ProcessContactEvent(event);
        if (!result.has_value())
        {
            continue;
        }

        const bool isLongPressUpdate = result->type == touchrev::gesture::GestureType::ThreeFingerLongPress &&
                                       result->phase == touchrev::gesture::GesturePhase::Update;
        if (isLongPressUpdate)
        {
            if (!accumulatedMoveResult.has_value())
            {
                accumulatedMoveResult = std::move(result);
            }
            else
            {
                accumulatedMoveResult->delta.x += result->delta.x;
                accumulatedMoveResult->delta.y += result->delta.y;
                accumulatedMoveResult->center = result->center;
                accumulatedMoveResult->contacts = std::move(result->contacts);
                accumulatedMoveResult->timestamp = result->timestamp;
            }
            continue;
        }

        if (!selectedResult.has_value())
        {
            selectedResult = std::move(result);
            continue;
        }

        const bool selectedIsBegin = selectedResult->type == touchrev::gesture::GestureType::ThreeFingerLongPress &&
                                     selectedResult->phase == touchrev::gesture::GesturePhase::Begin;
        if (!selectedIsBegin)
        {
            selectedResult = std::move(result);
        }
    }

    if (selectedResult.has_value())
    {
        return ConvertResult(*selectedResult);
    }

    if (accumulatedMoveResult.has_value())
    {
        return ConvertResult(*accumulatedMoveResult);
    }

    return {};
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::Tick()
{
    if (!session_)
    {
        return {};
    }

    session_->SetCurrentTimestamp(touchrev::gesture::CounterNow());

    std::optional<touchrev::gesture::GestureRecognitionResult> result;
    if (session_->Phase() == touchrev::gesture::GestureSessionPhase::Pending)
    {
        result = HandlePendingPhase();
    }

    if (!result.has_value() && session_ && session_->Phase() == touchrev::gesture::GestureSessionPhase::Active)
    {
        result = HandleActivePhase();
    }

    if (result.has_value())
    {
        return ConvertResult(*result);
    }

    return {};
}

void ThreeFingerGestureRecognizer::Reset()
{
    contactTracker_.Reset();
    session_.reset();
    longPressRecognizer_.Reset();
    tapRecognizer_.ResetAll();
    recognizersBegun_ = false;
    sessionStartIds_ = {};
    hasSessionStartIds_ = false;
    lastCenter_ = {};
    lastDelta_ = {};
    lastDistances_ = {};
    lastThreeFingerActive_ = false;
    lastSameHand_ = false;
    lastRawCenterX_ = 0;
    lastRawCenterY_ = 0;
    hasLastRawCenter_ = false;
}

double ThreeFingerGestureRecognizer::Distance(Point a, Point b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
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

std::optional<touchrev::gesture::GestureRecognitionResult> ThreeFingerGestureRecognizer::ProcessContactEvent(
    const touchrev::gesture::GestureContactEvent& event)
{
    if (!session_ && event.kind != touchrev::gesture::GestureContactEvent::Kind::Started)
    {
        return std::nullopt;
    }

    if (!session_)
    {
        session_ = std::make_unique<touchrev::gesture::GestureSession>(event.contact.timestamp);
        ResetRecognizersForSession();
        sessionStartIds_ = {};
        hasSessionStartIds_ = false;
    }

    switch (event.kind)
    {
    case touchrev::gesture::GestureContactEvent::Kind::Started:
        session_->AddContact(event.contact);
        if (session_->Phase() == touchrev::gesture::GestureSessionPhase::Active &&
            !session_->GestureRecognized &&
            session_->DurationMs() < 100.0 &&
            session_->TryUpgradeFingerCount(session_->CurrentFingerCount()))
        {
            ResetRecognizersForSession();
            if (session_->LockedFingerCount() == 3)
            {
                BeginRecognizers();
            }
        }
        break;
    case touchrev::gesture::GestureContactEvent::Kind::Updated:
        session_->UpdateContact(event.contact);
        break;
    case touchrev::gesture::GestureContactEvent::Kind::Ended:
        session_->RemoveContact(event.contact);
        break;
    }

    if (session_->IsAllFingersUp())
    {
        if (session_->Phase() == touchrev::gesture::GestureSessionPhase::Pending &&
            session_->PeakFingerCount() >= touchrev::gesture::GestureSession::kMinFingerCount &&
            session_->DurationMs() < kFastTrackTapMaxDurationMs)
        {
            if (session_->TryActivate() && session_->LockedFingerCount() == 3)
            {
                BeginRecognizers();
            }
        }

        return FinalizeSession(touchrev::gesture::GestureEndReason::AllFingersUp);
    }

    if (session_->Phase() == touchrev::gesture::GestureSessionPhase::Pending)
    {
        return HandlePendingPhase();
    }

    if (session_->Phase() == touchrev::gesture::GestureSessionPhase::Active)
    {
        return HandleActivePhase();
    }

    return std::nullopt;
}

std::optional<touchrev::gesture::GestureRecognitionResult> ThreeFingerGestureRecognizer::HandlePendingPhase()
{
    if (!session_)
    {
        return std::nullopt;
    }

    if (session_->IsRecognitionTimedOut())
    {
        return FinalizeSession(touchrev::gesture::GestureEndReason::RecognitionTimeout);
    }

    if (!session_->TryActivate())
    {
        return std::nullopt;
    }

    if (session_->LockedFingerCount() == 3)
    {
        BeginRecognizers();
    }

    return std::nullopt;
}

std::optional<touchrev::gesture::GestureRecognitionResult> ThreeFingerGestureRecognizer::HandleActivePhase()
{
    if (!session_)
    {
        return std::nullopt;
    }

    if (session_->IsFingerDropToleranceExpired(MinimumFingerQuorum()))
    {
        return FinalizeSession(touchrev::gesture::GestureEndReason::QuorumLostTimeout);
    }

    if (!recognizersBegun_ || session_->LockedFingerCount() != 3)
    {
        return std::nullopt;
    }

    const touchrev::gesture::GestureContext context = session_->BuildContext();
    std::optional<touchrev::gesture::GestureRecognitionResult> result = longPressRecognizer_.OnUpdate(context);
    if (result.has_value())
    {
        session_->GestureRecognized = true;
        session_->ActiveGestureType = result->type;
        return result;
    }

    tapRecognizer_.OnUpdate(context);
    return std::nullopt;
}

std::optional<touchrev::gesture::GestureRecognitionResult> ThreeFingerGestureRecognizer::FinalizeSession(
    touchrev::gesture::GestureEndReason)
{
    if (!session_)
    {
        return std::nullopt;
    }

    std::optional<touchrev::gesture::GestureRecognitionResult> result;
    const bool wasActive = session_->Phase() == touchrev::gesture::GestureSessionPhase::Active;
    if (wasActive && recognizersBegun_ && session_->LockedFingerCount() == 3)
    {
        const touchrev::gesture::GestureContext context = session_->BuildContext(true);
        result = longPressRecognizer_.OnEnd(context);
        if (!result.has_value())
        {
            result = tapRecognizer_.OnEnd(context);
        }

        if (result.has_value())
        {
            session_->GestureRecognized = true;
            session_->ActiveGestureType = result->type;
            session_->Complete();
        }
        else
        {
            session_->Cancel();
        }
    }
    else
    {
        longPressRecognizer_.Reset();
        tapRecognizer_.ResetCurrentSession();
        session_->Cancel();
    }

    session_.reset();
    recognizersBegun_ = false;
    return result;
}

void ThreeFingerGestureRecognizer::BeginRecognizers()
{
    if (!session_ || session_->LockedFingerCount() != 3)
    {
        return;
    }

    const touchrev::gesture::GestureContext context = session_->BuildContext(true);
    CaptureSessionStartIds(context.Contacts());
    ResetRecognizersForSession();
    longPressRecognizer_.OnBegin(context);
    tapRecognizer_.OnBegin(context);
    recognizersBegun_ = true;
}

void ThreeFingerGestureRecognizer::ResetRecognizersForSession()
{
    longPressRecognizer_.Reset();
    tapRecognizer_.ResetCurrentSession();
    recognizersBegun_ = false;
}

int ThreeFingerGestureRecognizer::MinimumFingerQuorum() const
{
    return session_ && session_->LockedFingerCount() > 0
               ? session_->LockedFingerCount()
               : touchrev::gesture::GestureSession::kMinFingerCount;
}

ThreeFingerGestureRecognizer::Result ThreeFingerGestureRecognizer::ConvertResult(
    const touchrev::gesture::GestureRecognitionResult& result)
{
    EventType type = EventType::None;
    if (result.type == touchrev::gesture::GestureType::ThreeFingerLongPress)
    {
        switch (result.phase)
        {
        case touchrev::gesture::GesturePhase::Begin:
            type = EventType::LongPressStarted;
            break;
        case touchrev::gesture::GesturePhase::Update:
            type = std::hypot(result.delta.x, result.delta.y) >= kMoveEpsilon
                       ? EventType::LongPressMoved
                       : EventType::LongPressHolding;
            break;
        case touchrev::gesture::GesturePhase::End:
            type = EventType::LongPressEnded;
            break;
        case touchrev::gesture::GesturePhase::Cancelled:
            type = EventType::None;
            break;
        }
    }
    else if (result.type == touchrev::gesture::GestureType::ThreeFingerDoubleTap)
    {
        type = EventType::DoubleTap;
    }

    UpdateRawCenterCache(result.contacts);
    const std::array<FingerPoint, 3> fingers = ExtractFingerPoints(result.contacts);
    const bool hasThreeFingers = result.contacts.size() >= 3;
    const Distances distances = hasThreeFingers ? CalculateDistances(fingers) : Distances{};
    const bool sameHand = hasThreeFingers ? IsSameHand(distances) : !result.contacts.empty();
    const bool active = result.phase != touchrev::gesture::GesturePhase::End && !result.contacts.empty();

    return MakeResult(
        type,
        active,
        sameHand,
        ToPublicPoint(result.center),
        ToPublicPoint(result.delta),
        distances,
        fingers);
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
        .startIds = sessionStartIds_,
        .deltaValid = hasSessionStartIds_,
        .rawCenterX = lastRawCenterX_,
        .rawCenterY = lastRawCenterY_,
        .hasRawCenter = hasLastRawCenter_,
    };
}

ThreeFingerGestureRecognizer::Point ThreeFingerGestureRecognizer::ToPublicPoint(touchrev::gesture::Point point)
{
    return {point.x, point.y};
}

std::array<ThreeFingerGestureRecognizer::FingerPoint, 3> ThreeFingerGestureRecognizer::ExtractFingerPoints(
    const std::vector<touchrev::gesture::GestureContact>& contacts)
{
    std::vector<touchrev::gesture::GestureContact> sorted = contacts;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.contactId < b.contactId;
    });

    std::array<FingerPoint, 3> fingers{};
    for (size_t i = 0; i < std::min(sorted.size(), fingers.size()); ++i)
    {
        fingers[i] = {
            .id = sorted[i].contactId,
            .point = {sorted[i].current.x, sorted[i].current.y},
            .rawX = sorted[i].rawX,
            .rawY = sorted[i].rawY,
        };
    }
    return fingers;
}

void ThreeFingerGestureRecognizer::CaptureSessionStartIds(const std::vector<touchrev::gesture::GestureContact>& contacts)
{
    const std::array<FingerPoint, 3> fingers = ExtractFingerPoints(contacts);
    sessionStartIds_ = {fingers[0].id, fingers[1].id, fingers[2].id};
    hasSessionStartIds_ = contacts.size() >= 3;
}

void ThreeFingerGestureRecognizer::UpdateRawCenterCache(const std::vector<touchrev::gesture::GestureContact>& contacts)
{
    if (contacts.empty())
    {
        return;
    }

    LONG sumX = 0;
    LONG sumY = 0;
    for (const touchrev::gesture::GestureContact& contact : contacts)
    {
        sumX += contact.rawX;
        sumY += contact.rawY;
    }

    lastRawCenterX_ = sumX / static_cast<LONG>(contacts.size());
    lastRawCenterY_ = sumY / static_cast<LONG>(contacts.size());
    hasLastRawCenter_ = true;
}
