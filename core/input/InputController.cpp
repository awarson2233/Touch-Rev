#include "InputController.h"

#include "common/Win32Error.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <sstream>
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

enum class ScreenRotation
{
    Rotation0 = 0,
    Rotation90 = 1,
    Rotation180 = 2,
    Rotation270 = 3
};

ScreenRotation RotationFromMonitor(HMONITOR monitor)
{
    if (!monitor)
    {
        return ScreenRotation::Rotation0;
    }

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi))
    {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        {
            return static_cast<ScreenRotation>(dm.dmDisplayOrientation);
        }
    }

    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm))
    {
        return static_cast<ScreenRotation>(dm.dmDisplayOrientation);
    }

    return ScreenRotation::Rotation0;
}
}

void InputController::Initialize(HWND hwnd)
{
    hwnd_ = hwnd;
    if (!EnableMouseInPointer(TRUE))
    {
        DebugLog(L"EnableMouseInPointer failed; mouse fallback messages remain enabled.");
    }

    if (!RegisterTouchWindow(hwnd, TWF_WANTPALM))
    {
        DebugLog(L"RegisterTouchWindow failed; WM_POINTER remains the primary touch path.");
    }

    rawTouchInput_.Initialize(hwnd);
}

void InputController::Cancel(HWND hwnd)
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
    dragOffsetX_ = 0.0f;
    dragOffsetY_ = 0.0f;
    ResetGesture();
    ResetSamples();
}

InputController::Result InputController::OnPointerDown(
    HWND hwnd,
    WPARAM wParam,
    const CoordinateSpace& coordinates,
    PointDip currentPosition,
    bool canStartDrag)
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INFO pointerInfo = {};
    if (!GetPointerInfo(pointerId, &pointerInfo))
    {
        DebugLog(L"GetPointerInfo failed for WM_POINTERDOWN message.");
        return {.handled = true};
    }

    const PointDip point = coordinates.ScreenPixelsToDips(hwnd, pointerInfo.ptPixelLocation);
    return BeginDrag(hwnd, point, PointerCounterOrNow(pointerInfo), currentPosition, true, pointerId, canStartDrag);
}

InputController::Result InputController::OnPointerUpdate(HWND hwnd, WPARAM wParam, const CoordinateSpace& coordinates)
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    if (!pointerDragging_ || pointerId != activePointerId_)
    {
        return {.handled = true};
    }

    return UpdateDragFromPointerHistory(hwnd, pointerId, coordinates);
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
        return {.handled = true, .dragEnded = true, .position = currentPosition_};
    }

    return {.handled = true};
}

InputController::Result InputController::OnMouseDown(
    HWND hwnd,
    LPARAM lParam,
    const CoordinateSpace& coordinates,
    PointDip currentPosition,
    bool canStartDrag)
{
    if (pointerDragging_ || touchDragging_)
    {
        return {.handled = false};
    }

    POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    const PointDip point = coordinates.ClientPixelsToDips(clientPoint);
    return BeginDrag(hwnd, point, QueryCounterValue(), currentPosition, false, 0, canStartDrag);
}

InputController::Result InputController::OnMouseMove(
    HWND,
    WPARAM wParam,
    LPARAM lParam,
    const CoordinateSpace& coordinates)
{
    if (!mouseDragging_ || (wParam & MK_LBUTTON) == 0)
    {
        return {.handled = false};
    }

    POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    return UpdateDrag(coordinates.ClientPixelsToDips(clientPoint), QueryCounterValue());
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
    const CoordinateSpace& coordinates,
    PointDip currentPosition,
    bool canStartDrag)
{
    const UINT inputCount = LOWORD(wParam);
    std::vector<TOUCHINPUT> inputs(inputCount);

    Result result{.handled = true, .position = currentPosition_};
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
            const Result begin = BeginDrag(hwnd, point, sampleQpc, currentPosition, false, 0, canStartDrag);
            if (begin.dragStarted)
            {
                touchDragging_ = true;
                mouseDragging_ = false;
                activeTouchId_ = input.dwID;
                result.dragStarted = true;
                result.position = begin.position;
            }
        }
        else if ((input.dwFlags & TOUCHEVENTF_MOVE) != 0 && touchDragging_ && input.dwID == activeTouchId_)
        {
            const Result update = UpdateDrag(point, sampleQpc);
            result.positionChanged = result.positionChanged || update.positionChanged;
            result.position = update.position;
        }
        else if ((input.dwFlags & TOUCHEVENTF_UP) != 0 && touchDragging_ && input.dwID == activeTouchId_)
        {
            const Result end = EndDrag(hwnd);
            result.dragEnded = result.dragEnded || end.dragEnded;
            result.position = end.position;
        }
    }

    CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
    return result;
}

std::vector<InputController::RawInputResult> InputController::OnRawInput(LPARAM lParam)
{
    std::vector<RawInputResult> results;
    for (const RawTouchInput::Frame& frame : rawTouchInput_.ProcessRawInput(lParam))
    {
        // 识别器的 contactId 不区分设备：会话进行中只接受发起会话的那块触摸屏。
        if (gestureRecognizer_.HasActiveSession() && frame.device != gestureDevice_)
        {
            continue;
        }

        const bool allContactsReleased = frame.AllContactsReleased();
        if (!frame.HasTouch() && !allContactsReleased)
        {
            continue;
        }

        if (!gestureRecognizer_.HasActiveSession())
        {
            gestureDevice_ = frame.device;
            gestureGeometry_ = frame.geometry;
        }

        const ThreeFingerGestureRecognizer::Result gestureResult = gestureRecognizer_.ProcessFrame(frame);
        results.push_back(MapGestureEvent(gestureResult, allContactsReleased));
    }
    return results;
}

InputController::RawInputResult InputController::OnGestureTick()
{
    // 无新输入帧时定时推进状态机：静止三指长按到 400ms 才能稳定进入长按。
    const ThreeFingerGestureRecognizer::Result gestureResult = gestureRecognizer_.Tick();
    return MapGestureEvent(gestureResult, false);
}

void InputController::ResetGesture()
{
    rawTouchInput_.Reset();
    gestureRecognizer_.Reset();
    gestureDevice_ = nullptr;
}

void InputController::OnInputDeviceChange(WPARAM wParam, LPARAM lParam)
{
    if (wParam != GIDC_REMOVAL)
    {
        return;
    }

    const HANDLE device = reinterpret_cast<HANDLE>(lParam);
    rawTouchInput_.RemoveDevice(device);
    if (device == gestureDevice_)
    {
        gestureRecognizer_.Reset();
        gestureDevice_ = nullptr;
    }
}

bool InputController::MapGestureCenterToScreen(LONG rawX, LONG rawY, bool hasRaw, POINT& screenPoint, int& rotation) const
{
    const RawTouchInput::DisplayMapping mapping = RawTouchInput::QueryDisplayMapping(gestureDevice_);
    rotation = static_cast<int>(mapping.monitor
                                    ? RotationFromMonitor(mapping.monitor)
                                    : RotationFromMonitor(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST)));

    const RECT& rect = mapping.displayRect;
    const LONG width = rect.right - rect.left;
    const LONG height = rect.bottom - rect.top;
    if (!hasRaw || !gestureGeometry_.logicalValid || width <= 0 || height <= 0)
    {
        return false;
    }

    // 先归一化到数字化仪自身方向，再按显示旋转转到屏幕方向；与 LongPressMoved 的增量旋转保持同一约定。
    const double u = std::clamp(
        static_cast<double>(rawX - gestureGeometry_.logicalMinX) / static_cast<double>(gestureGeometry_.logicalMaxX - gestureGeometry_.logicalMinX),
        0.0,
        1.0);
    const double v = std::clamp(
        static_cast<double>(rawY - gestureGeometry_.logicalMinY) / static_cast<double>(gestureGeometry_.logicalMaxY - gestureGeometry_.logicalMinY),
        0.0,
        1.0);

    double screenU = u;
    double screenV = v;
    switch (static_cast<ScreenRotation>(rotation))
    {
    case ScreenRotation::Rotation0:
        break;
    case ScreenRotation::Rotation90:
        screenU = 1.0 - v;
        screenV = u;
        break;
    case ScreenRotation::Rotation180:
        screenU = 1.0 - u;
        screenV = 1.0 - v;
        break;
    case ScreenRotation::Rotation270:
        screenU = v;
        screenV = 1.0 - u;
        break;
    }

    screenPoint.x = rect.left + static_cast<LONG>(std::lround(screenU * static_cast<double>(width - 1)));
    screenPoint.y = rect.top + static_cast<LONG>(std::lround(screenV * static_cast<double>(height - 1)));

    std::wstringstream log;
    log << L"[TouchCenter] raw=(" << rawX << L", " << rawY << L") screen=(" << screenPoint.x << L", " << screenPoint.y
        << L") rotation=" << rotation << L" fromPointerDevice=" << mapping.fromPointerDevice;
    DebugLog(log.str());
    return true;
}

InputController::RawInputResult InputController::MapGestureEvent(
    const ThreeFingerGestureRecognizer::Result& gestureResult,
    bool allContactsReleased)
{
    using EventType = ThreeFingerGestureRecognizer::EventType;

    if (gestureResult.type == EventType::DoubleTap)
    {
        // 双击通常在 release-all frame 触发，识别器保留了最后有效的触摸中心。
        POINT touchCenter{};
        int rotation = 0;
        const bool hasTouchCenter = MapGestureCenterToScreen(
            gestureResult.rawCenterX, gestureResult.rawCenterY, gestureResult.hasRawCenter, touchCenter, rotation);
        return {
            .handled = true,
            .action = InputAction::ShowSwitcher,
            .allContactsReleased = allContactsReleased,
            .touchCenterScreen = touchCenter,
            .hasTouchCenter = hasTouchCenter
        };
    }
    else if (gestureResult.type == EventType::LongPressStarted)
    {
        // 旋转在手势开始时算一次并缓存，整段长按内复用，避免每帧查询显示配置。
        POINT touchCenter{};
        int rotation = 0;
        const bool hasTouchCenter = MapGestureCenterToScreen(
            gestureResult.rawCenterX, gestureResult.rawCenterY, gestureResult.hasRawCenter, touchCenter, rotation);
        cachedGestureRotation_ = rotation;

        return {
            .handled = true,
            .action = InputAction::LongPressBegin,
            .allContactsReleased = allContactsReleased,
            .touchCenterScreen = touchCenter,
            .hasTouchCenter = hasTouchCenter
        };
    }
    else if (gestureResult.type == EventType::LongPressMoved)
    {
        const double dx = gestureResult.delta.x;
        const double dy = gestureResult.delta.y;

        double adjX = dx;
        double adjY = dy;
        switch (static_cast<ScreenRotation>(cachedGestureRotation_))
        {
        case ScreenRotation::Rotation0:
            adjX = dx;
            adjY = dy;
            break;
        case ScreenRotation::Rotation90:
            adjX = -dy;
            adjY = dx;
            break;
        case ScreenRotation::Rotation180:
            adjX = -dx;
            adjY = -dy;
            break;
        case ScreenRotation::Rotation270:
            adjX = dy;
            adjY = -dx;
            break;
        }

        return {
            .handled = true,
            .action = InputAction::LongPressMove,
            .deltaX = adjX,
            .deltaY = adjY,
            .allContactsReleased = allContactsReleased
        };
    }
    else if (gestureResult.type == EventType::LongPressEnded)
    {
        return {.handled = true, .action = InputAction::LongPressEnd, .allContactsReleased = allContactsReleased};
    }

    return {.handled = true, .allContactsReleased = allContactsReleased};
}

PointDip InputController::EvaluateVisualPosition() const
{
    if (!IsDragging() || !latestSample_.valid)
    {
        return currentPosition_;
    }

    const PointDip predictedPointer = PredictPointerPosition(QueryCounterValue());
    return {predictedPointer.x - dragOffsetX_, predictedPointer.y - dragOffsetY_};
}

void InputController::RebaseActiveDrag(HWND hwnd, PointDip currentPosition, const CoordinateSpace& coordinates)
{
    currentPosition_ = currentPosition;

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

    dragOffsetX_ = point.x - currentPosition_.x;
    dragOffsetY_ = point.y - currentPosition_.y;
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
    const CoordinateSpace& coordinates)
{
    POINTER_INFO latestInfo = {};
    if (!GetPointerInfo(pointerId, &latestInfo))
    {
        DebugLog(L"GetPointerInfo failed before pointer history lookup.");
        return {.handled = true, .position = currentPosition_};
    }

    constexpr UINT32 kMaxHistorySamples = 128;
    UINT32 entriesCount = std::clamp<UINT32>(latestInfo.historyCount, 1u, kMaxHistorySamples);
    std::vector<POINTER_INFO> history(entriesCount);

    UINT32 availableEntries = entriesCount;
    if (!GetPointerInfoHistory(pointerId, &availableEntries, history.data()))
    {
        const PointDip point = coordinates.ScreenPixelsToDips(hwnd, latestInfo.ptPixelLocation);
        return UpdateDrag(point, PointerCounterOrNow(latestInfo));
    }

    const UINT32 samplesToProcess = std::min<UINT32>(availableEntries, static_cast<UINT32>(history.size()));
    bool changed = false;
    for (UINT32 i = samplesToProcess; i > 0; --i)
    {
        const POINTER_INFO& sample = history[i - 1];
        const PointDip point = coordinates.ScreenPixelsToDips(hwnd, sample.ptPixelLocation);
        const Result update = UpdateDrag(point, PointerCounterOrNow(sample));
        changed = changed || update.positionChanged;
    }

    return {.handled = true, .positionChanged = changed, .position = currentPosition_};
}

InputController::Result InputController::BeginDrag(
    HWND hwnd,
    PointDip point,
    std::int64_t sampleQpc,
    PointDip currentPosition,
    bool pointerDrag,
    UINT32 pointerId,
    bool canStartDrag)
{
    if (!canStartDrag)
    {
        return {.handled = false, .position = currentPosition};
    }

    pointerDragging_ = pointerDrag;
    mouseDragging_ = !pointerDrag;
    touchDragging_ = false;
    activePointerId_ = pointerId;
    activeTouchId_ = 0;
    currentPosition_ = currentPosition;
    dragOffsetX_ = point.x - currentPosition_.x;
    dragOffsetY_ = point.y - currentPosition_.y;
    ResetSamples();
    RecordSample(point, sampleQpc);
    SetCapture(hwnd);

    return {.handled = true, .dragStarted = true, .position = currentPosition_};
}

InputController::Result InputController::UpdateDrag(PointDip point, std::int64_t sampleQpc)
{
    if (!pointerDragging_ && !mouseDragging_ && !touchDragging_)
    {
        return {.handled = false, .position = currentPosition_};
    }

    RecordSample(point, sampleQpc);

    const PointDip oldPosition = currentPosition_;
    currentPosition_ = {point.x - dragOffsetX_, point.y - dragOffsetY_};

    const bool changed = oldPosition.x != currentPosition_.x || oldPosition.y != currentPosition_.y;
    return {.handled = true, .positionChanged = changed, .position = currentPosition_};
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

    return {.handled = true, .dragEnded = true, .position = currentPosition_};
}

void InputController::ResetSamples()
{
    previousSample_ = {};
    latestSample_ = {};
    smoothedVelocityX_ = 0.0f;
    smoothedVelocityY_ = 0.0f;
    hasSmoothedVelocity_ = false;
}

void InputController::RecordSample(PointDip point, std::int64_t qpc)
{
    if (latestSample_.valid)
    {
        const double deltaSeconds = CounterSeconds(qpc - latestSample_.qpc);
        if (deltaSeconds >= 0.002 && deltaSeconds <= 0.060)
        {
            const float velocityX = static_cast<float>((point.x - latestSample_.point.x) / deltaSeconds);
            const float velocityY = static_cast<float>((point.y - latestSample_.point.y) / deltaSeconds);
            const float speed = std::hypot(velocityX, velocityY);
            if (speed >= 8.0f && speed <= 4000.0f)
            {
                constexpr float kVelocityBlend = 0.30f;
                if (hasSmoothedVelocity_)
                {
                    smoothedVelocityX_ = smoothedVelocityX_ * (1.0f - kVelocityBlend) + velocityX * kVelocityBlend;
                    smoothedVelocityY_ = smoothedVelocityY_ * (1.0f - kVelocityBlend) + velocityY * kVelocityBlend;
                }
                else
                {
                    smoothedVelocityX_ = velocityX;
                    smoothedVelocityY_ = velocityY;
                    hasSmoothedVelocity_ = true;
                }
            }
        }

        previousSample_ = latestSample_;
    }

    latestSample_ = {.point = point, .qpc = qpc, .valid = true};
}

PointDip InputController::PredictPointerPosition(std::int64_t nowQpc) const
{
    if (!latestSample_.valid || !previousSample_.valid || !hasSmoothedVelocity_)
    {
        return latestSample_.valid ? latestSample_.point : PointDip{};
    }

    const double sampleAgeSeconds = CounterSeconds(nowQpc - latestSample_.qpc);
    if (sampleAgeSeconds <= 0.0 || sampleAgeSeconds > 0.032)
    {
        return latestSample_.point;
    }

    const float speed = std::hypot(smoothedVelocityX_, smoothedVelocityY_);
    if (speed < 8.0f)
    {
        return latestSample_.point;
    }

    constexpr double kMaxPredictionSeconds = 0.008;
    constexpr float kMaxPredictionDistance = 6.0f;
    const double predictionSeconds = std::clamp(sampleAgeSeconds, 0.0, kMaxPredictionSeconds);
    float offsetX = smoothedVelocityX_ * static_cast<float>(predictionSeconds);
    float offsetY = smoothedVelocityY_ * static_cast<float>(predictionSeconds);
    const float offsetLength = std::hypot(offsetX, offsetY);
    if (offsetLength > kMaxPredictionDistance)
    {
        const float scale = kMaxPredictionDistance / offsetLength;
        offsetX *= scale;
        offsetY *= scale;
    }

    return {latestSample_.point.x + offsetX, latestSample_.point.y + offsetY};
}
