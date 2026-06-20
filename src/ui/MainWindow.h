#pragma once

#include "ThinXamlAppSwitcherHost.h"
#include "ThemeManager.h"
#include "appswitcher/MainView.h"
#include "common/CoordinateSpace.h"
#include "input/InputController.h"

#include <windows.h>

class MainWindow
{
public:
    enum class ActivationCommand : WPARAM
    {
        Show = 1,
        Hide = 2,
        Toggle = 3,
        Exit = 4,
    };

    static constexpr wchar_t WindowClassName[] = L"TouchRevGUI.MainWindow";
    static constexpr wchar_t WakeMessageName[] = L"TouchRevGUI.Wake";
    static constexpr wchar_t SingleInstanceMutexName[] = L"Local\\TouchRevGUI.SingleInstance";

    bool Initialize(HINSTANCE instance, int showCommand);
    int Run();
    void ShowSwitcher();
    void Hide();
    void ToggleSwitcher();
    void ExitApplication();
    bool IsSwitcherVisible() const { return isVisible_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    HRESULT OnCreate();
    void OnDestroy();
    void OnSize(UINT width, UINT height);
    void OnDpiChanged(WPARAM wParam, LPARAM lParam);
    void OnPaint();
    void RefreshTheme();
    void SyncClientLayout(UINT width, UINT height, bool renderSwitcher);
    void RebaseActiveDrag();
    void ForwardDpiChangeToChild(WPARAM wParam, LPARAM lParam);
    void HandleInputResult(const InputController::Result& result);
    void DispatchGestureAction(const InputController::RawInputResult& inputResult);
    bool HandleKeyDown(WPARAM key);
    HMONITOR ResolveTargetMonitor() const;
    MONITORINFO LoadMonitorInfo(HMONITOR monitor) const;

    bool CanStartDragFromPointer(WPARAM wParam) const;
    bool CanStartDragFromMouse(LPARAM lParam) const;

    static UINT ClientWidthFromLParam(LPARAM lParam);
    static UINT ClientHeightFromLParam(LPARAM lParam);
    static UINT GetClientWidth(HWND hwnd);
    static UINT GetClientHeight(HWND hwnd);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HMONITOR targetMonitor_ = nullptr;
    RECT targetMonitorRectPx_{};
    RECT targetWorkAreaPx_{};
    float dpi_ = 96.0f;
    UINT wakeMessage_ = 0;
    bool isVisible_ = false;
    bool isExiting_ = false;

    CoordinateSpace coordinates_;
    InputController inputController_;
    ThemeManager themeManager_;
    ThinXamlAppSwitcherHost xamlHost_;
    touchrev::appswitcher::MainView appSwitcherMainView_;
    bool isSyncingLayout_ = false;
    bool isLongPressNavigating_ = false;
    bool pendingLongPressActivation_ = false;
};
