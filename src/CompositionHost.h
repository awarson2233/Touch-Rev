#pragma once

#include <windows.h>
#include <objbase.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

class GraphicsDevice;

class CompositionHost
{
public:
    ~CompositionHost();

    HRESULT Initialize(HWND hwnd, GraphicsDevice& graphicsDevice, UINT width, UINT height);
    HRESULT Resize(UINT width, UINT height);
    void Reset();

    IDXGISwapChain1* SwapChain() const { return swapChain_.Get(); }
    IDCompositionDesktopDevice* CompositionDevice() const { return dcompDevice_.Get(); }
    HANDLE FrameLatencyWaitableObject() const { return frameLatencyWaitableObject_; }

private:
    HRESULT CreateSwapChain(GraphicsDevice& graphicsDevice, UINT width, UINT height);
    HRESULT CreateSwapChainWithFlags(GraphicsDevice& graphicsDevice, UINT flags);
    void ConfigureLowLatencySwapChain();
    void CloseFrameLatencyWaitableObject();

    Microsoft::WRL::ComPtr<IDCompositionDesktopDevice> dcompDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual2> rootVisual_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2_;
    HANDLE frameLatencyWaitableObject_ = nullptr;
    bool usesFrameLatencyWaitableObject_ = false;
    UINT width_ = 0;
    UINT height_ = 0;
};
