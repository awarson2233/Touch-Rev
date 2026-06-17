#include "GraphicsDevice.h"

#include "common/Win32Error.h"

#include <array>

namespace
{
constexpr std::array<D3D_FEATURE_LEVEL, 4> kFeatureLevels = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
};
}

HRESULT GraphicsDevice::Initialize()
{
    Reset();

    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = CreateD3DDevice(D3D_DRIVER_TYPE_HARDWARE, creationFlags);
#if defined(_DEBUG)
    if (FAILED(hr))
    {
        DebugLogHResult(L"D3D11CreateDevice(HARDWARE, DEBUG)", hr);
        creationFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = CreateD3DDevice(D3D_DRIVER_TYPE_HARDWARE, creationFlags);
    }
#endif

    if (FAILED(hr))
    {
        DebugLogHResult(L"D3D11CreateDevice(HARDWARE)", hr);
        hr = CreateD3DDevice(D3D_DRIVER_TYPE_WARP, D3D11_CREATE_DEVICE_BGRA_SUPPORT);
    }

    if (FAILED(hr))
    {
        DebugLogHResult(L"D3D11CreateDevice(WARP)", hr);
        Reset();
        return hr;
    }

    hr = d3dDevice_.As(&dxgiDevice_);
    if (FAILED(hr))
    {
        DebugLogHResult(L"ID3D11Device::QueryInterface(IDXGIDevice)", hr);
        Reset();
        return hr;
    }

    hr = dxgiDevice_->GetAdapter(dxgiAdapter_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDXGIDevice::GetAdapter", hr);
        Reset();
        return hr;
    }

    hr = dxgiAdapter_->GetParent(IID_PPV_ARGS(dxgiFactory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DebugLogHResult(L"IDXGIAdapter::GetParent(IDXGIFactory2)", hr);
        Reset();
        return hr;
    }

    hr = CreateDirect2DResources();
    if (FAILED(hr))
    {
        Reset();
        return hr;
    }

    return S_OK;
}

void GraphicsDevice::Reset()
{
    d2dContext_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    dxgiFactory_.Reset();
    dxgiAdapter_.Reset();
    dxgiDevice_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();
    featureLevel_ = D3D_FEATURE_LEVEL_11_0;
}

HRESULT GraphicsDevice::CreateD3DDevice(D3D_DRIVER_TYPE driverType, UINT creationFlags)
{
    return D3D11CreateDevice(
        nullptr,
        driverType,
        nullptr,
        creationFlags,
        kFeatureLevels.data(),
        static_cast<UINT>(kFeatureLevels.size()),
        D3D11_SDK_VERSION,
        d3dDevice_.ReleaseAndGetAddressOf(),
        &featureLevel_,
        d3dContext_.ReleaseAndGetAddressOf());
}

HRESULT GraphicsDevice::CreateDirect2DResources()
{
    D2D1_FACTORY_OPTIONS factoryOptions = {};
#if defined(_DEBUG)
    factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        &factoryOptions,
        reinterpret_cast<void**>(d2dFactory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DebugLogHResult(L"D2D1CreateFactory", hr);
        return hr;
    }

    hr = d2dFactory_->CreateDevice(dxgiDevice_.Get(), d2dDevice_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"ID2D1Factory1::CreateDevice", hr);
        return hr;
    }

    hr = d2dDevice_->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        d2dContext_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"ID2D1Device::CreateDeviceContext", hr);
        return hr;
    }

    return S_OK;
}
