#pragma once

#include "AppSwitcherLayoutEngine.h"
#include "ThinXamlAppSwitcherHost.h"
#include "common/CoordinateSpace.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

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
    winrt::Windows::UI::Xaml::Controls::Canvas layoutCanvas_{nullptr};
    winrt::Windows::UI::Xaml::Media::TranslateTransform layoutTransform_{nullptr};
    winrt::Windows::UI::Xaml::FrameworkElement focusBorder_{nullptr};
    winrt::Windows::UI::Xaml::FrameworkElement emptyGrid_{nullptr};
    std::vector<ItemView> items_;
    PointDip dragPosition_{};
    SizeDip contentBoundsDip_{};
    SizeDip clientSizeDip_{};
    bool xamlPointerDragging_ = false;
    double xamlDragOffsetX_ = 0.0;
    double xamlDragOffsetY_ = 0.0;
};
