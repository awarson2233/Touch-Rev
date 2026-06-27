#include "injector/inject.h"

#include "common/log.h"
#include "common/winutil.h"

#include <tlhelp32.h>

#include <cstdint>

namespace touchrev {
namespace {

RemoteModuleInfo EmptyRemoteModuleInfo() {
    return {};
}

bool ModuleMatchesDllPath(const MODULEENTRY32W& entry,
                          const std::wstring& fullDllPath,
                          bool* matchedByFullPath) {
    if (matchedByFullPath) {
        *matchedByFullPath = false;
    }

    if (!fullDllPath.empty() && EqualsIgnoreCase(entry.szExePath, fullDllPath)) {
        if (matchedByFullPath) {
            *matchedByFullPath = true;
        }
        return true;
    }

    return false;
}

bool FindRemoteModule(DWORD pid,
                      const std::wstring& fullDllPath,
                      RemoteModuleInfo* moduleInfo,
                      std::wstring* error) {
    if (moduleInfo) {
        *moduleInfo = EmptyRemoteModuleInfo();
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                               pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = L"CreateToolhelp32Snapshot failed: " +
                     FormatLastError(GetLastError());
        }
        return false;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            bool matchedByFullPath = false;
            if (!ModuleMatchesDllPath(entry, fullDllPath, &matchedByFullPath)) {
                continue;
            }

            RemoteModuleInfo current;
            current.module = reinterpret_cast<HMODULE>(entry.modBaseAddr);
            current.moduleName = entry.szModule;
            current.modulePath = entry.szExePath;
            current.matchedByFullPath = matchedByFullPath;

            if (moduleInfo) {
                *moduleInfo = current;
            }
            found = true;
            break;
        } while (Module32NextW(snapshot, &entry));
    } else {
        DWORD moduleError = GetLastError();
        if (moduleError != ERROR_NO_MORE_FILES) {
            if (error) {
                *error = L"Module32FirstW failed: " + FormatLastError(moduleError);
            }
            CloseHandle(snapshot);
            return false;
        }
    }

    CloseHandle(snapshot);

    if (!found && moduleInfo) {
        *moduleInfo = EmptyRemoteModuleInfo();
    }

    return true;
}

uintptr_t FindRemoteModuleBaseByName(DWORD pid, const std::wstring& moduleName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                               pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    uintptr_t base = 0;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (EqualsIgnoreCase(entry.szModule, moduleName)) {
                base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return base;
}

uintptr_t ResolveRemoteKernel32Export(DWORD pid,
                                      const char* exportName,
                                      std::wstring* error) {
    HMODULE localKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!localKernel32) {
        if (error) {
            *error = L"GetModuleHandleW(kernel32.dll) failed: " +
                     FormatLastError(GetLastError());
        }
        return 0;
    }

    FARPROC localExport = GetProcAddress(localKernel32, exportName);
    if (!localExport) {
        if (error) {
            *error = L"GetProcAddress(kernel32 export) failed: " +
                     FormatLastError(GetLastError());
        }
        return 0;
    }

    uintptr_t localBase = reinterpret_cast<uintptr_t>(localKernel32);
    uintptr_t localProc = reinterpret_cast<uintptr_t>(localExport);
    uintptr_t offset = localProc - localBase;

    uintptr_t remoteKernel32 = FindRemoteModuleBaseByName(pid, L"kernel32.dll");
    if (!remoteKernel32) {
        if (error) {
            *error = L"failed to find remote kernel32.dll module base";
        }
        return 0;
    }

    return remoteKernel32 + offset;
}

bool WaitForRemoteThread(HANDLE thread,
                         PCWSTR apiName,
                         DWORD* exitCode,
                         std::wstring* error) {
    DWORD wait = WaitForSingleObject(thread, 30000);
    if (wait != WAIT_OBJECT_0) {
        if (error) {
            *error = std::wstring(apiName ? apiName : L"remote thread") +
                     L" thread did not finish in 30s";
        }
        return false;
    }

    DWORD threadExitCode = 0;
    if (!GetExitCodeThread(thread, &threadExitCode)) {
        if (error) {
            *error = L"GetExitCodeThread failed: " + FormatLastError(GetLastError());
        }
        return false;
    }

    if (exitCode) {
        *exitCode = threadExitCode;
    }

    return true;
}

}  // namespace

bool QueryRemoteDllModule(DWORD pid,
                          const std::wstring& dllPath,
                          RemoteModuleInfo* moduleInfo,
                          std::wstring* error) {
    if (dllPath.empty()) {
        if (error) {
            *error = L"DLL path is empty";
        }
        return false;
    }

    return FindRemoteModule(pid, dllPath, moduleInfo, error);
}

bool InjectDllIntoProcess(DWORD pid,
                          const std::wstring& dllPath,
                          HMODULE* remoteModule,
                          std::wstring* error) {
    if (remoteModule) {
        *remoteModule = nullptr;
    }

    if (dllPath.empty()) {
        if (error) {
            *error = L"DLL path is empty";
        }
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                     PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        if (error) {
            *error = L"OpenProcess for injection failed: " +
                     FormatLastError(GetLastError());
        }
        return false;
    }

    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remotePath = VirtualAllocEx(process, nullptr, bytes,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        if (error) {
            *error = L"VirtualAllocEx failed: " + FormatLastError(GetLastError());
        }
        CloseHandle(process);
        return false;
    }

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(process, remotePath, dllPath.c_str(), bytes,
                            &bytesWritten) ||
        bytesWritten != bytes) {
        if (error) {
            *error = L"WriteProcessMemory failed: " + FormatLastError(GetLastError());
        }
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    std::wstring resolveError;
    uintptr_t remoteLoadLibrary = ResolveRemoteKernel32Export(pid, "LoadLibraryW",
                                                              &resolveError);
    if (!remoteLoadLibrary) {
        if (error) {
            *error = resolveError;
        }
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    LogMessage(L"injector", LogLevel::Info,
               L"event=INJECT_BEGIN pid=%lu dll=%s remoteLoadLibrary=0x%p",
               pid, dllPath.c_str(), reinterpret_cast<void*>(remoteLoadLibrary));

    HANDLE thread = CreateRemoteThread(
        process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteLoadLibrary), remotePath, 0,
        nullptr);
    if (!thread) {
        if (error) {
            *error = L"CreateRemoteThread failed: " + FormatLastError(GetLastError());
        }
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    DWORD exitCode = 0;
    bool threadOk = WaitForRemoteThread(thread, L"remote LoadLibraryW", &exitCode,
                                        error);
    CloseHandle(thread);

    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);

    if (!threadOk) {
        CloseHandle(process);
        return false;
    }

    RemoteModuleInfo moduleInfo;
    if (!FindRemoteModule(pid, dllPath, &moduleInfo, error) || !moduleInfo.module) {
        if (error && error->empty()) {
            *error = L"LoadLibraryW completed but injected module was not found. "
                     L"threadExitCode=0x" +
                     std::to_wstring(exitCode);
        }
        CloseHandle(process);
        return false;
    }

    if (remoteModule) {
        *remoteModule = moduleInfo.module;
    }

    LogMessage(L"injector", LogLevel::Info,
               L"event=INJECT_DONE pid=%lu remoteModule=0x%p threadExitCode=0x%08X "
               L"modulePath=%s matchedByFullPath=%d",
               pid, moduleInfo.module, exitCode, moduleInfo.modulePath.c_str(),
               moduleInfo.matchedByFullPath);

    CloseHandle(process);
    return true;
}

bool UninstallDllFromProcess(DWORD pid,
                             const std::wstring& dllPath,
                             RemoteModuleInfo* unloadedModule,
                             DWORD* freeLibraryResult,
                             std::wstring* error) {
    if (unloadedModule) {
        *unloadedModule = EmptyRemoteModuleInfo();
    }
    if (freeLibraryResult) {
        *freeLibraryResult = 0;
    }

    RemoteModuleInfo moduleInfo;
    if (!FindRemoteModule(pid, dllPath, &moduleInfo, error)) {
        return false;
    }

    if (!moduleInfo.module) {
        LogMessage(L"injector", LogLevel::Info,
                   L"event=UNINSTALL_SKIPPED pid=%lu dll=%s reason=not-loaded",
                   pid, dllPath.c_str());
        return true;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        if (error) {
            *error = L"OpenProcess for uninstall failed: " +
                     FormatLastError(GetLastError());
        }
        return false;
    }

    std::wstring resolveError;
    uintptr_t remoteFreeLibrary = ResolveRemoteKernel32Export(pid, "FreeLibrary",
                                                              &resolveError);
    if (!remoteFreeLibrary) {
        if (error) {
            *error = resolveError;
        }
        CloseHandle(process);
        return false;
    }

    LogMessage(L"injector", LogLevel::Info,
               L"event=UNINSTALL_BEGIN pid=%lu module=0x%p modulePath=%s "
               L"remoteFreeLibrary=0x%p",
               pid, moduleInfo.module, moduleInfo.modulePath.c_str(),
               reinterpret_cast<void*>(remoteFreeLibrary));

    HANDLE thread = CreateRemoteThread(
        process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteFreeLibrary),
        moduleInfo.module, 0, nullptr);
    if (!thread) {
        if (error) {
            *error = L"CreateRemoteThread(FreeLibrary) failed: " +
                     FormatLastError(GetLastError());
        }
        CloseHandle(process);
        return false;
    }

    DWORD exitCode = 0;
    bool threadOk = WaitForRemoteThread(thread, L"remote FreeLibrary", &exitCode,
                                        error);
    CloseHandle(thread);
    CloseHandle(process);

    if (!threadOk) {
        return false;
    }

    if (freeLibraryResult) {
        *freeLibraryResult = exitCode;
    }

    if (exitCode == 0) {
        if (error) {
            *error = L"remote FreeLibrary returned FALSE";
        }
        return false;
    }

    RemoteModuleInfo afterUnload;
    if (!FindRemoteModule(pid, dllPath, &afterUnload, error)) {
        return false;
    }

    if (afterUnload.module) {
        if (error) {
            *error = L"FreeLibrary returned TRUE but module is still loaded at 0x" +
                     std::to_wstring(reinterpret_cast<uintptr_t>(afterUnload.module));
        }
        return false;
    }

    if (unloadedModule) {
        *unloadedModule = moduleInfo;
    }

    LogMessage(L"injector", LogLevel::Info,
               L"event=UNINSTALL_DONE pid=%lu module=0x%p freeLibraryResult=%lu",
               pid, moduleInfo.module, exitCode);
    return true;
}

}  // namespace touchrev
