#pragma once

#include "CardView.h"
#include "ThinXamlAppSwitcherHost.h"
#include "thumbnail/PrivateThumbnailManager.h"

#include <windows.h>

#include <functional>
#include <memory>

namespace touchrev::appswitcher
{
// 卡片拖出后代替真实窗口进入系统移动循环的代理窗口。移动循环只能跑在窗口所属线程里，
// 代理与切换器同线程，拖动中由系统画贴边预览与 Snap Bar；松手后把系统给出的排列结果交给回调，
// 由调用方搬到真实窗口上。外观是一个独立 XAML 岛里的切换器卡片。
class DragProxyWindow
{
public:
    enum class ResultKind
    {
        Moved,
        Arranged,
        Maximized,
    };

    struct Result
    {
        HWND target = nullptr;
        ResultKind kind = ResultKind::Moved;
        // 系统给代理的排列矩形，已换算为可见边界（屏幕像素）。
        RECT arrangedRect{};
        POINT releasePoint{};
    };

    void SetFinishedCallback(std::function<void(const Result&)> callback);
    // 预先建好隐藏的代理窗口、XAML 岛与卡片模板，拖出时只剩换目标和显示。每个代理只用一次，
    // 用完销毁（系统会在窗口上留下排列状态），所以每次拖动结束后都要重新预备。
    bool Prepare(HINSTANCE instance);
    // 显示代理：大小取卡片矩形，位置让 grabPoint（最初按在卡片上的点）落在 pointer 下。均为屏幕像素。
    bool Show(HINSTANCE instance, HWND target, const RECT& cardRect, POINT grabPoint, POINT pointer);
    // 从 cursor 处让代理进入系统移动循环；循环同步运行，松手后回调结果并销毁代理。
    bool StartMove(POINT cursor);
    void Destroy();
    HWND Target() const { return target_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool BuildContent();
    bool BindTarget();
    void MoveGrabPointTo(POINT pointer);
    void OnSize(UINT width, UINT height);
    void OnWindowPosChanging(WINDOWPOS& pos);
    void Finish();

    HWND hwnd_ = nullptr;
    HWND target_ = nullptr;
    SIZE cardSizePx_{};
    // 最初按下的点相对卡片左上角的偏移（屏幕像素）。
    POINT grabOffset_{};
    // 代理窗口矩形比可见边界多出的不可见边框（左、上、右、下）。
    RECT frameInsets_{};
    bool arranged_ = false;
    RECT arrangedWindowRect_{};
    POINT releasePoint_{};
    std::function<void(const Result&)> finishedCallback_;

    ThinXamlAppSwitcherHost host_;
    std::unique_ptr<CardView> card_;
    // 系统靠近 Snap Bar 时会缩小被拖窗口，卡片按窗口尺寸等比缩放而不是重新排版。
    winrt::Windows::UI::Xaml::Media::ScaleTransform cardScale_{nullptr};
    // 缩略图设备跨多次拖动复用。
    touchrev::thumbnail::PrivateThumbnailManager thumbnailManager_;
};
}
