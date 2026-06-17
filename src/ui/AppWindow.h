#pragma once

#include "AnimationSystem.h"
#include "BackdropController.h"
#include "graphics/CompositionHost.h"
#include "common/CoordinateSpace.h"
#include "graphics/GraphicsDevice.h"
#include "input/InputController.h"
#include "model/RectangleModel.h"
#include "graphics/Renderer.h"

#include <windows.h>

class AppWindow
{
public:
    bool Initialize(HINSTANCE instance, int showCommand);
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    HRESULT OnCreate();
    void OnDestroy();
    void OnSize(UINT width, UINT height);
    void OnDpiChanged(WPARAM wParam, LPARAM lParam);
    void ResizeRenderTargets(UINT width, UINT height, bool render);
    void OnPaint();
    void OnAnimationTimer();
    void ApplyBackdropAndBackgroundMode();
    void PaintBackdropBase();

    HRESULT CreateGraphicsResources(bool initializeRectangle);
    void ReleaseGraphicsResources();
    void Render();
    void RenderFrames(UINT frameCount);
    void RequestRender();
    void OnRenderFrame();
    bool ShouldDriveRenderFrames() const;
    bool DispatchPendingMessages(MSG& message, int& exitCode);
    void HandleRenderFailure(HRESULT hr);
    void HandleInputResult(const InputController::Result& result);
    void StopAnimationForDirectManipulation();
    void StartReleaseAnimation();
    void StartAnimationTimer();
    void StopAnimationTimer();
    SizeDip CurrentClientSizeDips() const;
    static UINT ClientWidthFromLParam(LPARAM lParam);
    static UINT ClientHeightFromLParam(LPARAM lParam);

    static constexpr UINT_PTR kAnimationTimerId = 1;
    static constexpr UINT kAnimationTimerMs = 16;
    static constexpr DWORD kFallbackRenderFrameMs = 8;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    float dpi_ = 96.0f;
    bool graphicsReady_ = false;
    bool hasInitializedRectangle_ = false;
    bool animationTimerActive_ = false;
    bool renderRequested_ = false;

    BackdropController backdropController_;
    GraphicsDevice graphicsDevice_;
    CompositionHost compositionHost_;
    CoordinateSpace coordinates_;
    Renderer renderer_;
    RectangleModel rectangle_;
    InputController inputController_;
    AnimationSystem animationSystem_;
};
