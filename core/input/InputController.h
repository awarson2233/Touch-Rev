#pragma once

#include "common/CoordinateSpace.h"
#include "input/gesture/ThreeFingerGestureRecognizer.h"
#include "input/raw/RawTouchInput.h"

#include <windows.h>

#include <cstdint>

class InputController
{
public:
    struct Result
    {
        bool handled = false;
        bool positionChanged = false;
        bool dragStarted = false;
        bool dragEnded = false;
        PointDip position{};
    };

    enum class InputAction
    {
        None,
        ShowSwitcher,
        LongPressBegin,
        LongPressMove,
        LongPressEnd,
    };

    struct RawInputResult
    {
        bool handled = false;
        InputAction action = InputAction::None;
        double deltaX = 0.0;
        double deltaY = 0.0;
        bool allContactsReleased = false;
        POINT touchCenterScreen = {};  // 触摸中心屏幕坐标（用于定位显示器）
        bool hasTouchCenter = false;
    };

    void Initialize(HWND hwnd);
    void Cancel(HWND hwnd);

    Result OnPointerDown(HWND hwnd, WPARAM wParam, const CoordinateSpace& coordinates, PointDip currentPosition, bool canStartDrag);
    Result OnPointerUpdate(HWND hwnd, WPARAM wParam, const CoordinateSpace& coordinates);
    Result OnPointerUp(HWND hwnd, WPARAM wParam);
    Result OnPointerCaptureChanged(WPARAM wParam);

    Result OnMouseDown(HWND hwnd, LPARAM lParam, const CoordinateSpace& coordinates, PointDip currentPosition, bool canStartDrag);
    Result OnMouseMove(HWND hwnd, WPARAM wParam, LPARAM lParam, const CoordinateSpace& coordinates);
    Result OnMouseUp(HWND hwnd);

    Result OnTouch(HWND hwnd, WPARAM wParam, LPARAM lParam, const CoordinateSpace& coordinates, PointDip currentPosition, bool canStartDrag);
    RawInputResult OnRawInput(LPARAM lParam);
    RawInputResult OnGestureTick();

    bool IsDragging() const { return pointerDragging_ || mouseDragging_ || touchDragging_; }
    PointDip EvaluateVisualPosition() const;
    void RebaseActiveDrag(HWND hwnd, PointDip currentPosition, const CoordinateSpace& coordinates);

private:
    struct PointerSample
    {
        PointDip point{};
        std::int64_t qpc = 0;
        bool valid = false;
    };

    static bool TryGetPointerDip(HWND hwnd, UINT32 pointerId, const CoordinateSpace& coordinates, PointDip& point);

    Result UpdateDragFromPointerHistory(HWND hwnd, UINT32 pointerId, const CoordinateSpace& coordinates);
    Result BeginDrag(HWND hwnd, PointDip point, std::int64_t sampleQpc, PointDip currentPosition, bool pointerDrag, UINT32 pointerId, bool canStartDrag);
    Result UpdateDrag(PointDip point, std::int64_t sampleQpc);
    Result EndDrag(HWND hwnd);

    void ResetSamples();
    void RecordSample(PointDip point, std::int64_t qpc);
    PointDip PredictPointerPosition(std::int64_t nowQpc) const;

    // 把识别器事件映射成 RawInputResult。frame 为空表示来自定时 Tick（无新输入帧）。
    RawInputResult MapGestureEvent(
        const ThreeFingerGestureRecognizer::Result& gestureResult,
        const RawTouchInput::Frame* frame,
        bool allContactsReleased);

    HWND hwnd_ = nullptr;
    bool pointerDragging_ = false;
    bool mouseDragging_ = false;
    bool touchDragging_ = false;
    UINT32 activePointerId_ = 0;
    DWORD activeTouchId_ = 0;
    float dragOffsetX_ = 0.0f;
    float dragOffsetY_ = 0.0f;
    PointDip currentPosition_{};
    RawTouchInput rawTouchInput_;
    ThreeFingerGestureRecognizer gestureRecognizer_;
    PointerSample previousSample_{};
    PointerSample latestSample_{};
    float smoothedVelocityX_ = 0.0f;
    float smoothedVelocityY_ = 0.0f;
    bool hasSmoothedVelocity_ = false;
    // 长按手势期间缓存的显示旋转（0/90/180/270 对应 0..3），在 LongPressBegin 计算一次。
    int cachedGestureRotation_ = 0;
    // 长按手势开始时缓存的触摸中心屏幕坐标，用于确定目标显示器。
    POINT cachedGestureTouchCenter_ = {};
    bool hasCachedGestureTouchCenter_ = false;
};
