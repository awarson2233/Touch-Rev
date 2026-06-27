#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace touchrev {

std::wstring FormatLastError(DWORD error);
std::wstring GetWindowClassNameSafe(HWND hwnd);
bool WindowClassEquals(HWND hwnd, PCWSTR expectedClassName);
bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right);
std::wstring GetFileNameFromPath(const std::wstring& path);
std::wstring GetFullPath(const std::wstring& path);
bool GetPeMachineType(const std::wstring& path, USHORT* machine);
USHORT CurrentBuildMachineType();
PCWSTR CurrentBuildArchName();
PCWSTR MachineTypeToString(USHORT machine);

}  // namespace touchrev
