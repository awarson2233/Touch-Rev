#include "Renderer.h"

#include "CompositionHost.h"
#include "GraphicsDevice.h"
#include "common/Win32Error.h"

#include <d2d1_1helper.h>
#include <dxgi1_2.h>

HRESULT Renderer::Initialize(GraphicsDevice& graphicsDevice, CompositionHost& compositionHost, float dpi)
{
    graphicsDevice_ = &graphicsDevice;
    compositionHost_ = &compositionHost;
    dpi_ = dpi;
    return CreateTargetBitmap();
}

HRESULT Renderer::Resize(float dpi)
{
    dpi_ = dpi;
    ReleaseTarget();
    return CreateTargetBitmap();
}

HRESULT Renderer::Render(const RectangleModel& rectangle, const PointDip* visualPosition)
{
    if (!graphicsDevice_ || !compositionHost_ || !compositionHost_->SwapChain())
    {
        return E_FAIL;
    }

    if (!targetBitmap_)
    {
        const HRESULT hr = CreateTargetBitmap();
        if (FAILED(hr))
        {
            return hr;
        }
    }

    ID2D1DeviceContext* context = graphicsDevice_->D2DContext();
    context->SetTarget(targetBitmap_.Get());
    context->SetDpi(dpi_, dpi_);

    context->BeginDraw();
    if (backgroundMode_ == BackgroundMode::SolidWhiteFallback)
    {
        context->Clear(fallbackBackgroundColor_);
    }
    else
    {
        context->Clear(D2D1::ColorF(0.0f, 0.0f));
    }

    const PointDip position = visualPosition ? *visualPosition : rectangle.Position();
    const SizeDip size = rectangle.Size();
    const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
        D2D1::RectF(
            position.x,
            position.y,
            position.x + size.width,
            position.y + size.height),
        18.0f,
        18.0f);

    context->FillRoundedRectangle(&roundedRect, fillBrush_.Get());
    context->DrawRoundedRectangle(&roundedRect, strokeBrush_.Get(), 2.0f);

    HRESULT hr = context->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        ReleaseTarget();
        return hr;
    }
    if (FAILED(hr))
    {
        DebugLogHResult(L"ID2D1DeviceContext::EndDraw", hr);
        return hr;
    }

    DXGI_PRESENT_PARAMETERS presentParameters = {};
    hr = compositionHost_->SwapChain()->Present1(0, 0, &presentParameters);
    if (hr == DXGI_STATUS_OCCLUDED)
    {
        return S_OK;
    }
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDXGISwapChain1::Present1", hr);
    }

    return hr;
}

void Renderer::ReleaseTarget()
{
    if (graphicsDevice_ && graphicsDevice_->D2DContext())
    {
        graphicsDevice_->D2DContext()->SetTarget(nullptr);
    }
    targetBitmap_.Reset();
}

void Renderer::Reset()
{
    ReleaseTarget();
    fillBrush_.Reset();
    strokeBrush_.Reset();
    graphicsDevice_ = nullptr;
    compositionHost_ = nullptr;
    dpi_ = 96.0f;
}

HRESULT Renderer::CreateTargetBitmap()
{
    if (!graphicsDevice_ || !compositionHost_ || !compositionHost_->SwapChain())
    {
        return E_FAIL;
    }

    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    HRESULT hr = compositionHost_->SwapChain()->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()));
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDXGISwapChain1::GetBuffer", hr);
        return hr;
    }

    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpi_,
        dpi_);

    hr = graphicsDevice_->D2DContext()->CreateBitmapFromDxgiSurface(
        surface.Get(),
        &properties,
        targetBitmap_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"ID2D1DeviceContext::CreateBitmapFromDxgiSurface", hr);
        return hr;
    }

    graphicsDevice_->D2DContext()->SetTarget(targetBitmap_.Get());
    graphicsDevice_->D2DContext()->SetDpi(dpi_, dpi_);

    return EnsureBrushes();
}

HRESULT Renderer::EnsureBrushes()
{
    ID2D1DeviceContext* context = graphicsDevice_->D2DContext();

    if (!fillBrush_)
    {
        const HRESULT hr = context->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::DodgerBlue, 0.92f),
            fillBrush_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            DebugLogHResult(L"ID2D1DeviceContext::CreateSolidColorBrush(fill)", hr);
            return hr;
        }
    }

    if (!strokeBrush_)
    {
        const HRESULT hr = context->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White, 0.86f),
            strokeBrush_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            DebugLogHResult(L"ID2D1DeviceContext::CreateSolidColorBrush(stroke)", hr);
            return hr;
        }
    }

    return S_OK;
}

