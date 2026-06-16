#include "CompositionHost.h"

#include "GraphicsDevice.h"
#include "Win32Error.h"

#include <algorithm>

CompositionHost::~CompositionHost()
{
    Reset();
}

HRESULT CompositionHost::Initialize(HWND hwnd, GraphicsDevice& graphicsDevice, UINT width, UINT height)
{
    Reset();

    HRESULT hr = DCompositionCreateDevice2(
        graphicsDevice.DxgiDevice(),
        IID_PPV_ARGS(dcompDevice_.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DebugLogHResult(L"DCompositionCreateDevice2", hr);
        Reset();
        return hr;
    }

    hr = dcompDevice_->CreateTargetForHwnd(hwnd, TRUE, target_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDCompositionDesktopDevice::CreateTargetForHwnd", hr);
        Reset();
        return hr;
    }

    hr = dcompDevice_->CreateVisual(rootVisual_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDCompositionDesktopDevice::CreateVisual", hr);
        Reset();
        return hr;
    }

    hr = CreateSwapChain(graphicsDevice, width, height);
    if (FAILED(hr))
    {
        Reset();
        return hr;
    }

    hr = rootVisual_->SetContent(swapChain_.Get());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDCompositionVisual::SetContent", hr);
        Reset();
        return hr;
    }

    hr = target_->SetRoot(rootVisual_.Get());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDCompositionTarget::SetRoot", hr);
        Reset();
        return hr;
    }

    hr = dcompDevice_->Commit();
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDCompositionDevice::Commit", hr);
        Reset();
        return hr;
    }

    return S_OK;
}

HRESULT CompositionHost::Resize(UINT width, UINT height)
{
    if (!swapChain_)
    {
        return E_FAIL;
    }

    const UINT clampedWidth = std::max(1u, width);
    const UINT clampedHeight = std::max(1u, height);
    if (clampedWidth == width_ && clampedHeight == height_)
    {
        return S_OK;
    }

    const UINT resizeFlags = usesFrameLatencyWaitableObject_
                                 ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
                                 : 0;
    const HRESULT hr = swapChain_->ResizeBuffers(
        0,
        clampedWidth,
        clampedHeight,
        DXGI_FORMAT_UNKNOWN,
        resizeFlags);
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDXGISwapChain1::ResizeBuffers", hr);
        return hr;
    }

    width_ = clampedWidth;
    height_ = clampedHeight;
    return S_OK;
}

void CompositionHost::Reset()
{
    CloseFrameLatencyWaitableObject();
    swapChain2_.Reset();
    swapChain_.Reset();
    rootVisual_.Reset();
    target_.Reset();
    dcompDevice_.Reset();
    usesFrameLatencyWaitableObject_ = false;
    width_ = 0;
    height_ = 0;
}

HRESULT CompositionHost::CreateSwapChain(GraphicsDevice& graphicsDevice, UINT width, UINT height)
{
    width_ = std::max(1u, width);
    height_ = std::max(1u, height);

    HRESULT hr = CreateSwapChainWithFlags(graphicsDevice, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    if (FAILED(hr))
    {
        DebugLogHResult(L"CreateSwapChainForComposition(waitable)", hr);
        hr = CreateSwapChainWithFlags(graphicsDevice, 0);
        if (FAILED(hr))
        {
            DebugLogHResult(L"CreateSwapChainForComposition(default)", hr);
            return hr;
        }
    }

    ConfigureLowLatencySwapChain();
    return S_OK;
}

HRESULT CompositionHost::CreateSwapChainWithFlags(GraphicsDevice& graphicsDevice, UINT flags)
{
    CloseFrameLatencyWaitableObject();
    swapChain2_.Reset();
    swapChain_.Reset();
    usesFrameLatencyWaitableObject_ = (flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) != 0;

    DXGI_SWAP_CHAIN_DESC1 description = {};
    description.Width = width_;
    description.Height = height_;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.Stereo = FALSE;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    description.Flags = flags;

    HRESULT hr = graphicsDevice.DxgiFactory()->CreateSwapChainForComposition(
        graphicsDevice.D3DDevice(),
        &description,
        nullptr,
        swapChain_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        usesFrameLatencyWaitableObject_ = false;
    }

    return hr;
}

void CompositionHost::ConfigureLowLatencySwapChain()
{
    if (!usesFrameLatencyWaitableObject_ || !swapChain_)
    {
        return;
    }

    HRESULT hr = swapChain_.As(&swapChain2_);
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDXGISwapChain1::QueryInterface(IDXGISwapChain2)", hr);
        return;
    }

    hr = swapChain2_->SetMaximumFrameLatency(1);
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDXGISwapChain2::SetMaximumFrameLatency", hr);
        return;
    }

    frameLatencyWaitableObject_ = swapChain2_->GetFrameLatencyWaitableObject();
    if (!frameLatencyWaitableObject_)
    {
        DebugLog(L"IDXGISwapChain2::GetFrameLatencyWaitableObject returned NULL.");
    }
}

void CompositionHost::CloseFrameLatencyWaitableObject()
{
    if (frameLatencyWaitableObject_)
    {
        CloseHandle(frameLatencyWaitableObject_);
        frameLatencyWaitableObject_ = nullptr;
    }
}
