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

bool UninstallDllFromProcess(DWORD pid,
                             const std::wstring& dllPath,
                             RemoteModuleInfo* unloadedModule,
                             DWORD* freeLibraryResult,
                             std::wstring* error);

}  // namespace touchrev
