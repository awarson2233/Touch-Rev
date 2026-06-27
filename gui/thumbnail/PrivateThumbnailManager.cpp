#include "PrivateThumbnailManager.h"

#include "common/Win32Error.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

#include <algorithm>
#include <roapi.h>
#include <sstream>

namespace touchrev::thumbnail
{
namespace
{
void LogHr(std::wstring_view context, HRESULT hr)
{
    if (FAILED(hr))
    {
        DebugLogHResult(context, hr);
    }
}

FoundationSize MakeCreateSize(double widthDip, double heightDip, double dpiScale)
{
    constexpr double kThumbnailBackingScale = 2.0;
    const double safeScale = std::max(0.01, dpiScale);
    return {
        static_cast<float>(std::max(1.0, widthDip * safeScale * kThumbnailBackingScale)),
        static_cast<float>(std::max(1.0, heightDip * safeScale * kThumbnailBackingScale))};
}
}

bool PrivateThumbnailManager::EnsureDevice()
{
    if (device_)
    {
        return true;
    }

    winrt::hstring className{kDesktopThumbnailDeviceClass};
    HRESULT hr = RoActivateInstance(
        static_cast<HSTRING>(winrt::get_abi(className)),
        reinterpret_cast<IInspectable**>(deviceInspectable_.put_void()));
    LogHr(L"RoActivateInstance(DesktopThumbnailDevice)", hr);
    if (FAILED(hr) || !deviceInspectable_)
    {
        return false;
    }

    hr = deviceInspectable_->QueryInterface(kIDesktopThumbnailDevice, device_.put_void());
    LogHr(L"QueryInterface(IDesktopThumbnailDevice)", hr);
    return SUCCEEDED(hr) && device_;
}

PrivateThumbnailSlot PrivateThumbnailManager::CreateForWindow(
    HWND hwnd,
    winrt::Windows::UI::Xaml::FrameworkElement const& hostElement,
    double widthDip,
    double heightDip,
    double dpiScale)
{
    using namespace winrt::Windows::UI::Composition;
    using namespace winrt::Windows::UI::Xaml::Hosting;

    PrivateThumbnailSlot slot;
    slot.hwnd = hwnd;
    slot.displayWidthDip = widthDip;
    slot.displayHeightDip = heightDip;
    slot.dpiScale = dpiScale;

    if (hwnd == nullptr || !hostElement)
    {
        slot.lastError = E_INVALIDARG;
        return slot;
    }

    if (!EnsureDevice())
    {
        slot.lastError = E_FAIL;
        return slot;
    }

    HRESULT hr = device_->CreateThumbnailFactoryForWindow(
        reinterpret_cast<void*>(hwnd),
        reinterpret_cast<IInspectable**>(slot.factoryInspectable.put_void()));
    LogHr(L"CreateThumbnailFactoryForWindow", hr);
    if (FAILED(hr) || !slot.factoryInspectable)
    {
        slot.lastError = FAILED(hr) ? hr : E_POINTER;
        return slot;
    }

    hr = slot.factoryInspectable->QueryInterface(kIThumbnailFactory, slot.visualFactory.put_void());
    LogHr(L"QueryInterface(IThumbnailFactory visual ABI)", hr);
    if (FAILED(hr) || !slot.visualFactory)
    {
        slot.lastError = FAILED(hr) ? hr : E_POINTER;
        return slot;
    }

    FoundationSize sourceSize{};
    hr = slot.visualFactory->get_SourceSize(&sourceSize);
    LogHr(L"IThumbnailFactory::get_SourceSize", hr);

    FoundationRect sourceBounds{};
    hr = slot.visualFactory->get_Bounds(&sourceBounds);
    LogHr(L"IThumbnailFactory::get_Bounds", hr);

    Visual hostVisual = ElementCompositionPreview::GetElementVisual(hostElement);
    const Compositor compositor = hostVisual.Compositor();
    if (!compositor)
    {
        slot.lastError = E_FAIL;
        return slot;
    }

    ThumbnailProperties properties{};
    properties.value8 = 0x101;
    const FoundationSize createSize = MakeCreateSize(widthDip, heightDip, dpiScale);
    slot.createWidthDip = createSize.Width;
    slot.createHeightDip = createSize.Height;
    auto* compositorAbi = reinterpret_cast<IUnknown*>(winrt::get_abi(compositor));
    hr = slot.visualFactory->CreateThumbnailVisual(
        compositorAbi,
        createSize,
        properties,
        reinterpret_cast<IInspectable**>(slot.duplicateVisual.put_void()));
    LogHr(L"IThumbnailFactory::CreateThumbnailVisual", hr);
    if (FAILED(hr) || !slot.duplicateVisual)
    {
        slot.lastError = FAILED(hr) ? hr : E_POINTER;
        return slot;
    }

    winrt::com_ptr<IDuplicateVisualProbe> duplicateVisualProbe;
    hr = slot.duplicateVisual->QueryInterface(kIDuplicateVisual, duplicateVisualProbe.put_void());
    LogHr(L"QueryInterface(IDuplicateVisual)", hr);
    if (FAILED(hr) || !duplicateVisualProbe)
    {
        slot.lastError = FAILED(hr) ? hr : E_POINTER;
        return slot;
    }

    winrt::com_ptr<IInspectable> innerVisual;
    hr = duplicateVisualProbe->get_DuplicateVisual(reinterpret_cast<IInspectable**>(innerVisual.put_void()));
    LogHr(L"IDuplicateVisual::get_DuplicateVisual", hr);
    if (FAILED(hr) || !innerVisual)
    {
        slot.lastError = FAILED(hr) ? hr : E_POINTER;
        return slot;
    }

    void* rawVisual = nullptr;
    hr = innerVisual->QueryInterface(
        winrt::guid_of<winrt::Windows::UI::Composition::IVisual>(),
        &rawVisual);
    LogHr(L"Inner QueryInterface(Windows.UI.Composition.IVisual)", hr);
    if (FAILED(hr) || rawVisual == nullptr)
    {
        slot.lastError = FAILED(hr) ? hr : E_POINTER;
        return slot;
    }

    slot.visual = Visual{nullptr};
    winrt::attach_abi(slot.visual, rawVisual);

    slot.wrapperVisual = compositor.CreateSpriteVisual();
    slot.clipGeometry = compositor.CreateRoundedRectangleGeometry();
    slot.geometricClip = compositor.CreateGeometricClip(slot.clipGeometry);
    slot.wrapperVisual.Clip(slot.geometricClip);
    slot.wrapperVisual.Children().InsertAtTop(slot.visual);
    ElementCompositionPreview::SetElementChildVisual(hostElement, slot.wrapperVisual);

    ResizeSlot(slot, widthDip, heightDip);
    slot.lastError = S_OK;

    std::wstringstream log;
    log << L"Thumbnail attached hwnd=" << hwnd << L" sizeDip=" << widthDip << L"x" << heightDip;
    DebugLog(log.str());
    return slot;
}

void PrivateThumbnailManager::ClearSlot(
    winrt::Windows::UI::Xaml::FrameworkElement const& hostElement,
    PrivateThumbnailSlot& slot)
{
    using namespace winrt::Windows::UI::Xaml::Hosting;

    if (hostElement)
    {
        ElementCompositionPreview::SetElementChildVisual(hostElement, nullptr);
    }

    if (slot.wrapperVisual)
    {
        slot.wrapperVisual.Children().RemoveAll();
    }

    slot.visual = nullptr;
    slot.geometricClip = nullptr;
    slot.clipGeometry = nullptr;
    slot.wrapperVisual = nullptr;
    slot.duplicateVisual = nullptr;
    slot.visualFactory = nullptr;
    slot.factoryInspectable = nullptr;
    slot.hwnd = nullptr;
    slot.lastError = S_OK;
    slot.displayWidthDip = 0.0;
    slot.displayHeightDip = 0.0;
    slot.createWidthDip = 0.0;
    slot.createHeightDip = 0.0;
    slot.dpiScale = 1.0;
}

void PrivateThumbnailManager::ResizeSlot(PrivateThumbnailSlot& slot, double widthDip, double heightDip)
{
    if (!slot.wrapperVisual || !slot.visual)
    {
        return;
    }

    slot.displayWidthDip = widthDip;
    slot.displayHeightDip = heightDip;

    constexpr float bottomBleedDip = 1.0f;
    const float displayWidth = static_cast<float>(std::max(0.0, widthDip));
    const float displayHeight = static_cast<float>(std::max(0.0, heightDip));
    const float visualHeight = displayHeight + bottomBleedDip;
    const float createWidth = static_cast<float>(std::max(1.0, slot.createWidthDip));
    const float createHeight = static_cast<float>(std::max(1.0, slot.createHeightDip));

    slot.wrapperVisual.Offset({0.0f, 0.0f, 0.0f});
    slot.wrapperVisual.Size({displayWidth, visualHeight});
    if (slot.clipGeometry)
    {
        constexpr float cornerRadius = 10.0f;
        slot.clipGeometry.Offset({0.0f, -cornerRadius});
        slot.clipGeometry.Size({displayWidth, visualHeight + cornerRadius});
        slot.clipGeometry.CornerRadius({cornerRadius, cornerRadius});
    }

    slot.visual.Offset({0.0f, 0.0f, 0.0f});
    slot.visual.Size({createWidth, createHeight});
    slot.visual.Scale({
        createWidth > 0.0f ? displayWidth / createWidth : 1.0f,
        createHeight > 0.0f ? visualHeight / createHeight : 1.0f,
        1.0f});
}
}
