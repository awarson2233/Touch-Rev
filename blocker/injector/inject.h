#pragma once

#include <windows.h>

#include <string>

namespace touchrev {

struct RemoteModuleInfo {
    HMODULE module{};
    std::wstring moduleName;
    std::wstring modulePath;
    bool matchedByFullPath{};
};

bool QueryRemoteDllModule(DWORD pid,
                          const std::wstring& dllPath,
                          RemoteModuleInfo* moduleInfo,
                          std::wstring* error);

bool InjectDllIntoProcess(DWORD pid,
                          const std::wstring& dllPath,
                          HMODULE* remoteModule,
                          std::wstring* error);

bool StartHookInProcess(DWORD pid,
                        const std::wstring& dllPath,
                        HMODULE remoteModule,
                        std::wstring* error);

// unloadDeferred 置真表示 Hook 仍在初始化（通常在下载 PDB），停止已登记为延后执行，
// 初始化结束后 DLL 会自行恢复手势表并卸载；此时返回 true 且不调用 FreeLibrary。
bool UninstallDllFromProcess(DWORD pid,
                             const std::wstring& dllPath,
                             RemoteModuleInfo* unloadedModule,
                             DWORD* freeLibraryResult,
                             std::wstring* error,
                             bool* unloadDeferred = nullptr);

}  // namespace touchrev
