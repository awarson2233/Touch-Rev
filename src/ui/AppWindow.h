#pragma once

#include "AppSwitcherXamlView.h"
#include "BackdropController.h"
#include "ThinXamlAppSwitcherHost.h"
#include "ThemeManager.h"
#include "common/CoordinateSpace.h"
#include "input/InputController.h"

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
    void OnPaint();
    void ApplyBackdropAndBackgroundMode();
    void RefreshTheme();
    void UpdateTransparentRegion();
    void HandleInputResult(const InputController::Result& result);

    bool CanStartDragFromPointer(WPARAM wParam) const;
    bool CanStartDragFromMouse(LPARAM lParam) const;

    static UINT ClientWidthFromLParam(LPARAM lParam);
    static UINT ClientHeightFromLParam(LPARAM lParam);
    static UINT GetClientWidth(HWND hwnd);
    static UINT GetClientHeight(HWND hwnd);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    float dpi_ = 96.0f;

    BackdropController backdropController_;
    CoordinateSpace coordinates_;
    InputController inputController_;
    ThemeManager themeManager_;
    ThinXamlAppSwitcherHost xamlHost_;
    AppSwitcherXamlView appSwitcherXamlView_;
};
