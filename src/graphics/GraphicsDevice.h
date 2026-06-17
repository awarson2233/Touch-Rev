#pragma once

#include <windows.h>
#include <objbase.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

class GraphicsDevice
{
public:
    HRESULT Initialize();
    void Reset();

    ID3D11Device* D3DDevice() const { return d3dDevice_.Get(); }
    ID3D11DeviceContext* D3DContext() const { return d3dContext_.Get(); }
    IDXGIDevice* DxgiDevice() const { return dxgiDevice_.Get(); }
    IDXGIFactory2* DxgiFactory() const { return dxgiFactory_.Get(); }
    ID2D1DeviceContext* D2DContext() const { return d2dContext_.Get(); }

private:
    HRESULT CreateD3DDevice(D3D_DRIVER_TYPE driverType, UINT creationFlags);
    HRESULT CreateDirect2DResources();

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice_;
    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
};
