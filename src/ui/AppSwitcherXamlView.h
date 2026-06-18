#pragma once

#include "AppSwitcherLayoutEngine.h"
#include "ThinXamlAppSwitcherHost.h"
#include "common/CoordinateSpace.h"
#include "thumbnail/PrivateThumbnailManager.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <memory>
#include <string>
#include <vector>

class AppSwitcherXamlView
{
public:
    bool Initialize(HWND hwnd, ThinXamlAppSwitcherHost& host);
    void Shutdown();
    void Resize(UINT widthPx, UINT heightPx, double scale);
    void RenderSample(UINT widthPx, UINT heightPx, double scale);
    bool HitTest(PointDip point) const;
    PointDip DragPosition() const { return dragPosition_; }
    void SetDragPosition(PointDip position);

private:
    struct ItemView
    {
        winrt::Windows::UI::Xaml::FrameworkElement root{nullptr};
        winrt::Windows::UI::Xaml::Controls::TextBlock title{nullptr};
        winrt::Windows::UI::Xaml::Controls::Button closeButton{nullptr};
        winrt::Windows::UI::Xaml::FrameworkElement thumbnailHost{nullptr};
        HWND hwnd = nullptr;
        PointDip layoutPosition{};
        std::unique_ptr<touchrev::thumbnail::PrivateThumbnailSlot> thumbnailSlot;
    };

    bool LoadRoot();
    ItemView CreateItem();
    void EnsureItemCount(size_t count);
    void ApplyLayout(const std::vector<AppSwitcherWindowItem>& windows, UINT widthPx, UINT heightPx, double scale);
    void AttachPointerHandlers();

    static std::wstring LoadTextFileUtf8(const std::wstring& path);
    static std::wstring ModuleRelativePath(const std::wstring& relativePath);

    HWND hwnd_ = nullptr;
    ThinXamlAppSwitcherHost* host_ = nullptr;
    bool initialized_ = false;

    winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
    winrt::Windows::UI::Xaml::FrameworkElement appSwitcherContainer_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Canvas layoutCanvas_{nullptr};
    winrt::Windows::UI::Xaml::FrameworkElement focusBorder_{nullptr};
    winrt::Windows::UI::Xaml::FrameworkElement emptyGrid_{nullptr};
    std::vector<ItemView> items_;
    PointDip dragPosition_{};
    SizeDip contentBoundsDip_{};
    SizeDip clientSizeDip_{};
    bool xamlPointerDragging_ = false;
    size_t activeDragItemIndex_ = static_cast<size_t>(-1);
    double xamlDragOffsetX_ = 0.0;
    double xamlDragOffsetY_ = 0.0;
    double currentDpiScale_ = 1.0;
    touchrev::thumbnail::PrivateThumbnailManager thumbnailManager_;
};
