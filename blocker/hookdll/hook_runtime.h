#pragma once

#include <windows.h>

extern "C" __declspec(dllexport) DWORD WINAPI TouchRevHookStart(LPVOID reserved);
// 返回 ERROR_IO_PENDING 表示初始化线程仍在运行（通常在下载 PDB），
// 停止已登记为延后执行：初始化结束后由该线程恢复手势表并自行卸载 DLL，调用方不得再 FreeLibrary。
extern "C" __declspec(dllexport) DWORD WINAPI TouchRevHookStop(LPVOID reserved);

namespace touchrev {
// DllMain 记录自身模块句柄，延后停止时用于 FreeLibraryAndExitThread。
void SetHookModule(HMODULE module);
}
