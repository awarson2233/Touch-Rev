#include "InputController.h"

#include "Win32Error.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
std::int64_t QueryCounterValue()
{
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

double CounterSeconds(std::int64_t delta)
{
    static const double frequency = []() {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();

    return static_cast<double>(delta) / frequency;
}

std::int64_t PointerCounterOrNow(const POINTER_INFO& pointerInfo)
{
    return pointerInfo.PerformanceCount != 0
               ? static_cast<std::int64_t>(pointerInfo.PerformanceCount)
               : QueryCounterValue();
}
}

void InputController::Initialize(HWND hwnd)
{
    if (!EnableMouseInPointer(TRUE))
    {
        DebugLog(L"EnableMouseInPointer failed; mouse fallback messages remain enabled.");
    }

    if (!RegisterTouchWindow(hwnd, TWF_WANTPALM))
    {
        DebugLog(L"RegisterTouchWindow failed; WM_POINTER remains the primary touch path.");
    }
}

void InputController::Cancel(HWND hwnd)
{
    if (pointerDragging_ || mouseDragging_ || touchDragging_)
    {
        ReleaseCapture();
    }

    pointerDragging_ = false;
    mouseDragging_ = false;
    touchDragging_ = false;
    activePointerId_ = 0;
    activeTouchId_ = 0;
    dragOffsetX_ = 0.0f;
    dragOffsetY_ = 0.0f;
    ResetSamples();

    if (GetCapture() == hwnd)
    {
        ReleaseCapture();
    }
}

InputController::Result InputController::OnPointerDown(
    HWND hwnd,
    WPARAM wParam,
    RectangleModel& rectangle,
    const CoordinateSpace& coordinates)
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INFO pointerInfo = {};
    if (!GetPointerInfo(pointerId, &pointerInfo))
    {
        DebugLog(L"GetPointerInfo failed for WM_POINTERDOWN message.");
        return {.handled = true};
    }

    const PointDip point = coordinates.ScreenPixelsToDips(hwnd, pointerInfo.ptPixelLocation);
    return BeginDrag(hwnd, point, PointerCounterOrNow(pointerInfo), rectangle, true, pointerId);
}

InputController::Result InputController::OnPointerUpdate(
    HWND hwnd,
    WPARAM wParam,
    RectangleModel& rectangle,
    const CoordinateSpace& coordinates)
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    if (!pointerDragging_ || pointerId != activePointerId_)
    {
        return {.handled = true};
    }

    return UpdateDragFromPointerHistory(hwnd, pointerId, rectangle, coordinates);
}

InputController::Result InputController::OnPointerUp(HWND hwnd, WPARAM wParam)
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    if (!pointerDragging_ || pointerId != activePointerId_)
    {
        return {.handled = true};
    }

    return EndDrag(hwnd);
}

InputController::Result InputController::OnPointerCaptureChanged(WPARAM wParam)
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    if (pointerDragging_ && pointerId == activePointerId_)
    {
        pointerDragging_ = false;
        activePointerId_ = 0;
        ResetSamples();
        return {.handled = true, .dragEnded = true};
    }

    return {.handled = true};
}

InputController::Result InputController::OnMouseDown(
    HWND hwnd,
    LPARAM lParam,
    RectangleModel& rectangle,
    const CoordinateSpace& coordinates)
{
    if (pointerDragging_ || touchDragging_)
    {
        return {.handled = false};
    }

    POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    const PointDip point = coordinates.ClientPixelsToDips(clientPoint);
    return BeginDrag(hwnd, point, QueryCounterValue(), rectangle, false, 0);
}

InputController::Result InputController::OnMouseMove(
    HWND,
    WPARAM wParam,
    LPARAM lParam,
    RectangleModel& rectangle,
    const CoordinateSpace& coordinates)
{
    if (!mouseDragging_ || (wParam & MK_LBUTTON) == 0)
    {
        return {.handled = false};
    }

    POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    return UpdateDrag(coordinates.ClientPixelsToDips(clientPoint), QueryCounterValue(), rectangle);
}

InputController::Result InputController::OnMouseUp(HWND hwnd)
{
    if (!mouseDragging_)
    {
        return {.handled = false};
    }

    return EndDrag(hwnd);
}

InputController::Result InputController::OnTouch(
    HWND hwnd,
    WPARAM wParam,
    LPARAM lParam,
    RectangleModel& rectangle,
    const CoordinateSpace& coordinates)
{
    const UINT inputCount = LOWORD(wParam);
    std::vector<TOUCHINPUT> inputs(inputCount);

    Result result{.handled = true};
    if (inputCount == 0 ||
        !GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lParam), inputCount, inputs.data(), sizeof(TOUCHINPUT)))
    {
        CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
        return result;
    }

    for (const TOUCHINPUT& input : inputs)
    {
        if (pointerDragging_ || mouseDragging_)
        {
            continue;
        }

        POINT screenPoint{TOUCH_COORD_TO_PIXEL(input.x), TOUCH_COORD_TO_PIXEL(input.y)};
        const PointDip point = coordinates.ScreenPixelsToDips(hwnd, screenPoint);
        const std::int64_t sampleQpc = QueryCounterValue();

        if ((input.dwFlags & TOUCHEVENTF_DOWN) != 0 && !touchDragging_)
        {
            if (rectangle.HitTest(point))
            {
                touchDragging_ = true;
                activeTouchId_ = input.dwID;
                dragOffsetX_ = point.x - rectangle.Left();
                dragOffsetY_ = point.y - rectangle.Top();
                ResetSamples();
                RecordSample(point, sampleQpc);
                SetCapture(hwnd);
                result.dragStarted = true;
            }
        }
        else if ((input.dwFlags & TOUCHEVENTF_MOVE) != 0 && touchDragging_ && input.dwID == activeTouchId_)
        {
            const Result update = UpdateDrag(point, sampleQpc, rectangle);
            result.rectangleChanged = result.rectangleChanged || update.rectangleChanged;
        }
        else if ((input.dwFlags & TOUCHEVENTF_UP) != 0 && touchDragging_ && input.dwID == activeTouchId_)
        {
            const Result end = EndDrag(hwnd);
            result.dragEnded = result.dragEnded || end.dragEnded;
        }
    }

    CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
    return result;
}

PointDip InputController::EvaluateVisualPosition(const RectangleModel& rectangle) const
{
    if (!IsDragging() || !latestSample_.valid)
    {
        return rectangle.Position();
    }

    const PointDip predictedPointer = PredictPointerPosition(QueryCounterValue());
    const PointDip predictedPosition{
        predictedPointer.x - dragOffsetX_,
        predictedPointer.y - dragOffsetY_};
    return rectangle.ClampPosition(predictedPosition);
}

void InputController::RebaseActiveDrag(HWND hwnd, const RectangleModel& rectangle, const CoordinateSpace& coordinates)
{
    PointDip point{};
    if (pointerDragging_ && activePointerId_ != 0)
    {
        if (!TryGetPointerDip(hwnd, activePointerId_, coordinates, point))
        {
            return;
        }
    }
    else if (mouseDragging_)
    {
        POINT cursor{};
        if (!GetCursorPos(&cursor))
        {
            return;
        }
        point = coordinates.ScreenPixelsToDips(hwnd, cursor);
    }
    else
    {
        ResetSamples();
        return;
    }

    dragOffsetX_ = point.x - rectangle.Left();
    dragOffsetY_ = point.y - rectangle.Top();
    ResetSamples();
    RecordSample(point, QueryCounterValue());
}

bool InputController::TryGetPointerDip(HWND hwnd, UINT32 pointerId, const CoordinateSpace& coordinates, PointDip& point)
{
    POINTER_INFO pointerInfo = {};
    if (!GetPointerInfo(pointerId, &pointerInfo))
    {
        DebugLog(L"GetPointerInfo failed for WM_POINTER message.");
        return false;
    }

    point = coordinates.ScreenPixelsToDips(hwnd, pointerInfo.ptPixelLocation);
    return true;
}

InputController::Result InputController::UpdateDragFromPointerHistory(
    HWND hwnd,
    UINT32 pointerId,
    RectangleModel& rectangle,
    const CoordinateSpace& coordinates)
{
    POINTER_INFO latestInfo = {};
    if (!GetPointerInfo(pointerId, &latestInfo))
    {
        DebugLog(L"GetPointerInfo failed before pointer history lookup.");
        return {.handled = true};
    }

    constexpr UINT32 kMaxHistorySamples = 128;
    UINT32 entriesCount = std::clamp<UINT32>(latestInfo.historyCount, 1u, kMaxHistorySamples);
    std::vector<POINTER_INFO> history(entriesCount);

    UINT32 availableEntries = entriesCount;
    if (!GetPointerInfoHistory(pointerId, &availableEntries, history.data()))
    {
        const PointDip point = coordinates.ScreenPixelsToDips(hwnd, latestInfo.ptPixelLocation);
        return UpdateDrag(point, PointerCounterOrNow(latestInfo), rectangle);
    }

    const UINT32 samplesToProcess = std::min<UINT32>(availableEntries, static_cast<UINT32>(history.size()));
    bool changed = false;
    for (UINT32 i = samplesToProcess; i > 0; --i)
    {
        const POINTER_INFO& sample = history[i - 1];
        const PointDip point = coordinates.ScreenPixelsToDips(hwnd, sample.ptPixelLocation);
        const Result update = UpdateDrag(point, PointerCounterOrNow(sample), rectangle);
        changed = changed || update.rectangleChanged;
    }

    return {.handled = true, .rectangleChanged = changed};
}

InputController::Result InputController::BeginDrag(
    HWND hwnd,
    PointDip point,
    std::int64_t sampleQpc,
    RectangleModel& rectangle,
    bool pointerDrag,
    UINT32 pointerId)
{
    if (!rectangle.HitTest(point))
    {
        return {.handled = false};
    }

    pointerDragging_ = pointerDrag;
    mouseDragging_ = !pointerDrag;
    touchDragging_ = false;
    activePointerId_ = pointerId;
    activeTouchId_ = 0;
    dragOffsetX_ = point.x - rectangle.Left();
    dragOffsetY_ = point.y - rectangle.Top();
    ResetSamples();
    RecordSample(point, sampleQpc);
    SetCapture(hwnd);

    return {.handled = true, .dragStarted = true};
}

InputController::Result InputController::UpdateDrag(PointDip point, std::int64_t sampleQpc, RectangleModel& rectangle)
{
    if (!pointerDragging_ && !mouseDragging_ && !touchDragging_)
    {
        return {.handled = false};
    }

    RecordSample(point, sampleQpc);

    const PointDip nextPosition{point.x - dragOffsetX_, point.y - dragOffsetY_};
    const PointDip oldPosition = rectangle.Position();
    rectangle.MoveTo(nextPosition);
    const PointDip newPosition = rectangle.Position();

    const bool changed = oldPosition.x != newPosition.x || oldPosition.y != newPosition.y;
    return {.handled = true, .rectangleChanged = changed};
}

InputController::Result InputController::EndDrag(HWND hwnd)
{
    if (GetCapture() == hwnd)
    {
        ReleaseCapture();
    }

    pointerDragging_ = false;
    mouseDragging_ = false;
    touchDragging_ = false;
    activePointerId_ = 0;
    activeTouchId_ = 0;
    ResetSamples();

    return {.handled = true, .dragEnded = true};
}

void InputController::ResetSamples()
{
    previousSample_ = {};
    latestSample_ = {};
}

void InputController::RecordSample(PointDip point, std::int64_t qpc)
{
    if (latestSample_.valid)
    {
        previousSample_ = latestSample_;
    }

    latestSample_ = {.point = point, .qpc = qpc, .valid = true};
}

PointDip InputController::PredictPointerPosition(std::int64_t nowQpc) const
{
    if (!latestSample_.valid || !previousSample_.valid)
    {
        return latestSample_.valid ? latestSample_.point : PointDip{};
    }

    const double sampleDeltaSeconds = CounterSeconds(latestSample_.qpc - previousSample_.qpc);
    if (sampleDeltaSeconds <= 0.0)
    {
        return latestSample_.point;
    }

    const double sampleAgeSeconds = CounterSeconds(nowQpc - latestSample_.qpc);
    if (sampleAgeSeconds <= 0.0 || sampleAgeSeconds > 0.040)
    {
        return latestSample_.point;
    }

    const float velocityX = static_cast<float>((latestSample_.point.x - previousSample_.point.x) / sampleDeltaSeconds);
    const float velocityY = static_cast<float>((latestSample_.point.y - previousSample_.point.y) / sampleDeltaSeconds);
    const float speed = std::hypot(velocityX, velocityY);
    if (speed < 8.0f)
    {
        return latestSample_.point;
    }

    constexpr double kMaxPredictionSeconds = 0.012;
    const double predictionSeconds = std::clamp(sampleAgeSeconds, 0.0, kMaxPredictionSeconds);
    return {
        latestSample_.point.x + velocityX * static_cast<float>(predictionSeconds),
        latestSample_.point.y + velocityY * static_cast<float>(predictionSeconds)};
}
