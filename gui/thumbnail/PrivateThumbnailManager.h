#pragma once

#include "PrivateThumbnailInterfaces.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.h>

#include <windows.h>

namespace touchrev::thumbnail
{
struct PrivateThumbnailSlot
{
    HWND hwnd = nullptr;
    winrt::com_ptr<IInspectable> factoryInspectable;
    winrt::com_ptr<IThumbnailFactoryVisual> visualFactory;
    winrt::com_ptr<IInspectable> duplicateVisual;
    winrt::Windows::UI::Composition::SpriteVisual wrapperVisual{nullptr};
    winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry clipGeometry{nullptr};
    winrt::Windows::UI::Composition::CompositionGeometricClip geometricClip{nullptr};
    winrt::Windows::UI::Composition::Visual visual{nullptr};
    HRESULT lastError = S_OK;
    double displayWidthDip = 0.0;
    double displayHeightDip = 0.0;
    double createWidthDip = 0.0;
    double createHeightDip = 0.0;
    double dpiScale = 1.0;

    explicit operator bool() const { return wrapperVisual != nullptr && visual != nullptr; }
};

class PrivateThumbnailManager
{
public:
    bool EnsureDevice();

    PrivateThumbnailSlot CreateForWindow(
        HWND hwnd,
        winrt::Windows::UI::Xaml::FrameworkElement const& hostElement,
        double widthDip,
        double heightDip,
        double dpiScale);

    static void ResizeSlot(PrivateThumbnailSlot& slot, double widthDip, double heightDip);
    static void ClearSlot(
        winrt::Windows::UI::Xaml::FrameworkElement const& hostElement,
        PrivateThumbnailSlot& slot);

private:
    winrt::com_ptr<IInspectable> deviceInspectable_;
    winrt::com_ptr<IDesktopThumbnailDevice> device_;
};
}
