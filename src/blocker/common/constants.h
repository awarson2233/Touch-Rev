#pragma once

#include <windows.h>

namespace touchrev {

inline constexpr UINT kWorkerGestureMessage = 0xC029;
inline constexpr UINT kShellTrayThreeFingerLongPressMessage = 0x05C6;
inline constexpr WPARAM kWorkerGestureBegin = 0x35;
inline constexpr WPARAM kWorkerGestureEnd = 0x36;
inline constexpr DWORD kDefaultBlockDurationMs = 6000;

inline constexpr wchar_t kClassShellTrayWnd[] = L"Shell_TrayWnd";
inline constexpr wchar_t kClassWorkerW[] = L"WorkerW";
inline constexpr wchar_t kClassAppThumbnailWindow[] = L"AppThumbnailWindow";

}  // namespace touchrev
