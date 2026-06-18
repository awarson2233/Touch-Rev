#include "BackdropController.h"

#include "common/Win32Error.h"

#include <dwmapi.h>

namespace
{
#if defined(DWMWA_USE_IMMERSIVE_DARK_MODE)
constexpr DWORD kDwmwaUseImmersiveDarkMode = DWMWA_USE_IMMERSIVE_DARK_MODE;
#else
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
#endif

#if defined(DWMWA_WINDOW_CORNER_PREFERENCE)
constexpr DWORD kDwmwaWindowCornerPreference = DWMWA_WINDOW_CORNER_PREFERENCE;
#else
constexpr DWORD kDwmwaWindowCornerPreference = 33;
#endif

#if defined(DWMWA_SYSTEMBACKDROP_TYPE)
constexpr DWORD kDwmwaSystemBackdropType = DWMWA_SYSTEMBACKDROP_TYPE;
#else
constexpr DWORD kDwmwaSystemBackdropType = 38;
#endif

#if defined(DWMWCP_DONOTROUND)
constexpr int kDwmwcpDoNotRound = DWMWCP_DONOTROUND;
#else
constexpr int kDwmwcpDoNotRound = 1;
#endif

#if defined(DWMWA_BORDER_COLOR)
constexpr DWORD kDwmwaBorderColor = DWMWA_BORDER_COLOR;
#else
constexpr DWORD kDwmwaBorderColor = 34;
#endif

#if defined(DWMSBT_NONE)
constexpr int kDwmsbtNone = DWMSBT_NONE;
#else
constexpr int kDwmsbtNone = 1;
#endif

#if defined(DWMWA_COLOR_NONE)
constexpr COLORREF kDwmColorNone = DWMWA_COLOR_NONE;
#else
constexpr COLORREF kDwmColorNone = 0xFFFFFFFE;
#endif
}

bool BackdropController::ShouldUseDarkMode()
{
    DWORD appsUseLightTheme = 1;
    DWORD valueSize = sizeof(appsUseLightTheme);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &appsUseLightTheme,
        &valueSize);

    return status == ERROR_SUCCESS && appsUseLightTheme == 0;
}

BackdropApplyResult BackdropController::Apply(HWND hwnd, bool useDarkMode) const
{
    BackdropApplyResult result{};

    BOOL darkMode = useDarkMode ? TRUE : FALSE;
    result.darkModeHr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaUseImmersiveDarkMode,
        &darkMode,
        sizeof(darkMode));
    if (FAILED(result.darkModeHr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)", result.darkModeHr);
    }

    int cornerPreference = kDwmwcpDoNotRound;
    result.cornerHr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaWindowCornerPreference,
        &cornerPreference,
        sizeof(cornerPreference));
    if (FAILED(result.cornerHr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)", result.cornerHr);
    }

    COLORREF borderColor = kDwmColorNone;
    const HRESULT borderColorHr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaBorderColor,
        &borderColor,
        sizeof(borderColor));
    if (FAILED(borderColorHr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_BORDER_COLOR)", borderColorHr);
    }

    int backdrop = kDwmsbtNone;
    result.systemBackdropHr = DwmSetWindowAttribute(
        hwnd,
        kDwmwaSystemBackdropType,
        &backdrop,
        sizeof(backdrop));
    if (FAILED(result.systemBackdropHr))
    {
        DebugLogHResult(L"DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)", result.systemBackdropHr);
    }

    result.extendFrameHr = S_OK;

    result.systemBackdropApplied = SUCCEEDED(result.systemBackdropHr);
    return result;
}
