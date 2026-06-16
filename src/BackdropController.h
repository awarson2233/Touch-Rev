#pragma once

#include <windows.h>

struct BackdropApplyResult
{
    HRESULT darkModeHr = S_OK;
    HRESULT cornerHr = S_OK;
    HRESULT systemBackdropHr = S_OK;
    HRESULT extendFrameHr = S_OK;
    bool systemBackdropApplied = false;
};

class BackdropController
{
public:
    static bool ShouldUseDarkMode();

    BackdropApplyResult Apply(HWND hwnd, bool useDarkMode) const;
};
