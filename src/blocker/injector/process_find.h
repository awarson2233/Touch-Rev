#pragma once

#include <windows.h>

#include <string>

namespace touchrev {

struct TargetProcess {
    HWND shellTrayHwnd{};
    DWORD pid{};
    std::wstring imagePath;
    USHORT processMachine{};
    USHORT nativeMachine{};
};

bool QueryProcessMachine(HANDLE process,
                         USHORT* processMachine,
                         USHORT* nativeMachine,
                         std::wstring* error);
bool IsNativeMachineForCurrentBuild(USHORT processMachine, USHORT nativeMachine);
bool IsCurrentProcessNativeForCurrentBuild(std::wstring* error);
bool FindShellExplorer(TargetProcess* target, std::wstring* error);

}  // namespace touchrev
