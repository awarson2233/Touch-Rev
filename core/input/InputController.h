#pragma once

#include "common/CoordinateSpace.h"
#include "input/gesture/ThreeFingerGestureRecognizer.h"
#include "input/raw/RawTouchInput.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <vector>

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
    // 一条 WM_INPUT 可能完成多帧，每帧各产出一个结果，按顺序分发。
    std::vector<RawInputResult> OnRawInput(LPARAM lParam);
    // 每块触摸屏各自推进一次状态机，按设备顺序返回结果。
    std::vector<RawInputResult> OnGestureTick();
    // 任一触摸屏有手势会话进行中才需要定时推进状态机。
    bool NeedsGestureTick() const;
    // 丢弃原始触点与识别器状态（例如手势识别被关闭时）。
    void ResetGesture();
    void OnInputDeviceChange(WPARAM wParam, LPARAM lParam);

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

    // 每块触摸屏独立识别：contactId 按设备各自从 0 起编，共用识别器会互相覆盖，
    // 而只锁定一块屏又会让另一块屏在会话未结束时完全失效。
    struct DeviceGesture
    {
        HANDLE device = nullptr;
        RawTouchInput::DeviceGeometry geometry{};
        ThreeFingerGestureRecognizer recognizer;
        // 长按手势期间缓存的显示旋转（0/90/180/270 对应 0..3），在 LongPressBegin 计算一次。
        int cachedRotation = 0;
    };

    DeviceGesture& GetDeviceGesture(HANDLE device);

    static bool TryGetPointerDip(HWND hwnd, UINT32 pointerId, const CoordinateSpace& coordinates, PointDip& point);

    Result UpdateDragFromPointerHistory(HWND hwnd, UINT32 pointerId, const CoordinateSpace& coordinates);
    Result BeginDrag(HWND hwnd, PointDip point, std::int64_t sampleQpc, PointDip currentPosition, bool pointerDrag, UINT32 pointerId, bool canStartDrag);
    Result UpdateDrag(PointDip point, std::int64_t sampleQpc);
    Result EndDrag(HWND hwnd);

    void ResetSamples();
    void RecordSample(PointDip point, std::int64_t qpc);
    PointDip PredictPointerPosition(std::int64_t nowQpc) const;

    // 把识别器事件映射成 RawInputResult。
    RawInputResult MapGestureEvent(
        DeviceGesture& gesture,
        const ThreeFingerGestureRecognizer::Result& gestureResult,
        bool allContactsReleased);
    // 按手势所在触摸屏的量程与显示映射，把 HID 逻辑坐标换算为屏幕像素，并给出该屏的旋转。
    bool MapGestureCenterToScreen(const DeviceGesture& gesture, LONG rawX, LONG rawY, bool hasRaw, POINT& screenPoint, int& rotation) const;

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
    // 元素按需创建；用 unique_ptr 保证扩容时已有识别器的地址不变。
    std::vector<std::unique_ptr<DeviceGesture>> deviceGestures_;
    PointerSample previousSample_{};
    PointerSample latestSample_{};
    float smoothedVelocityX_ = 0.0f;
    float smoothedVelocityY_ = 0.0f;
    bool hasSmoothedVelocity_ = false;
};
