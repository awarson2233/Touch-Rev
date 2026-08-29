#pragma once

#include <windows.h>

extern "C" __declspec(dllexport) DWORD WINAPI TouchRevHookStart(LPVOID reserved);
extern "C" __declspec(dllexport) DWORD WINAPI TouchRevHookStop(LPVOID reserved);

