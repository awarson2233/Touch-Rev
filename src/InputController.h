#pragma once

#include "CoordinateSpace.h"
#include "RectangleModel.h"

#include <windows.h>

#include <cstdint>

class InputController
{
public:
    struct Result
    {
        bool handled = false;
        bool rectangleChanged = false;
        bool dragStarted = false;
        bool dragEnded = false;
    };

    void Initialize(HWND hwnd);
    void Cancel(HWND hwnd);

    Result OnPointerDown(HWND hwnd, WPARAM wParam, RectangleModel& rectangle, const CoordinateSpace& coordinates);
    Result OnPointerUpdate(HWND hwnd, WPARAM wParam, RectangleModel& rectangle, const CoordinateSpace& coordinates);
    Result OnPointerUp(HWND hwnd, WPARAM wParam);
    Result OnPointerCaptureChanged(WPARAM wParam);

    Result OnMouseDown(HWND hwnd, LPARAM lParam, RectangleModel& rectangle, const CoordinateSpace& coordinates);
    Result OnMouseMove(HWND hwnd, WPARAM wParam, LPARAM lParam, RectangleModel& rectangle, const CoordinateSpace& coordinates);
    Result OnMouseUp(HWND hwnd);

    Result OnTouch(HWND hwnd, WPARAM wParam, LPARAM lParam, RectangleModel& rectangle, const CoordinateSpace& coordinates);

    bool IsDragging() const { return pointerDragging_ || mouseDragging_ || touchDragging_; }
    PointDip EvaluateVisualPosition(const RectangleModel& rectangle) const;
    void RebaseActiveDrag(HWND hwnd, const RectangleModel& rectangle, const CoordinateSpace& coordinates);

private:
    struct PointerSample
    {
        PointDip point{};
        std::int64_t qpc = 0;
        bool valid = false;
    };

    static bool TryGetPointerDip(HWND hwnd, UINT32 pointerId, const CoordinateSpace& coordinates, PointDip& point);

    Result UpdateDragFromPointerHistory(HWND hwnd, UINT32 pointerId, RectangleModel& rectangle, const CoordinateSpace& coordinates);
    Result BeginDrag(HWND hwnd, PointDip point, std::int64_t sampleQpc, RectangleModel& rectangle, bool pointerDrag, UINT32 pointerId);
    Result UpdateDrag(PointDip point, std::int64_t sampleQpc, RectangleModel& rectangle);
    Result EndDrag(HWND hwnd);

    void ResetSamples();
    void RecordSample(PointDip point, std::int64_t qpc);
    PointDip PredictPointerPosition(std::int64_t nowQpc) const;

    bool pointerDragging_ = false;
    bool mouseDragging_ = false;
    bool touchDragging_ = false;
    UINT32 activePointerId_ = 0;
    DWORD activeTouchId_ = 0;
    float dragOffsetX_ = 0.0f;
    float dragOffsetY_ = 0.0f;
    PointerSample previousSample_{};
    PointerSample latestSample_{};
};
