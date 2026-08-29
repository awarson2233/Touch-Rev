#include "injector/inject.h"

#include "common/log.h"
#include "common/winutil.h"

#include <tlhelp32.h>

#include <cstdint>
#include <new>

namespace touchrev {
namespace {

constexpr char kHookStartExport[] = "TouchRevHookStart";
constexpr char kHookStopExport[] = "TouchRevHookStop";
constexpr DWORD kLoadLibraryTimeoutMs = 30000;
constexpr DWORD kHookControlTimeoutMs = 30000;

struct RemoteThreadWaitResult {
    bool threadFinished = false;
    bool resultAvailable = false;
    DWORD exitCode = 0;
};

struct DeferredRemoteFreeContext {
    HANDLE process = nullptr;
    HANDLE remoteThread = nullptr;
    LPVOID remoteAddress = nullptr;
    DWORD pid = 0;
};

DWORD WINAPI DeferredRemoteFreeThread(LPVOID parameter) {
    auto* context = static_cast<DeferredRemoteFreeContext*>(parameter);
    if (!context) {
        return ERROR_INVALID_PARAMETER;
    }

    const DWORD wait = WaitForSingleObject(context->remoteThread, INFINITE);
    if (wait == WAIT_OBJECT_0 && context->remoteAddress) {
        VirtualFreeEx(context->process, context->remoteAddress, 0, MEM_RELEASE);
    }

    if (context->remoteThread) {
        CloseHandle(context->remoteThread);
    }
    if (context->process) {
        CloseHandle(context->process);
    }
    delete context;
    return wait == WAIT_OBJECT_0 ? ERROR_SUCCESS : ERROR_GEN_FAILURE;
}

bool ScheduleDeferredRemoteFree(HANDLE process,
                                HANDLE remoteThread,
                                LPVOID remoteAddress,
                                DWORD pid) {
    auto* context = new (std::nothrow) DeferredRemoteFreeContext{
        process, remoteThread, remoteAddress, pid};
    if (!context) {
        return false;
    }

    HANDLE cleanupThread = CreateThread(
        nullptr, 0, DeferredRemoteFreeThread, context, 0, nullptr);
    if (!cleanupThread) {
        delete context;
        return false;
    }

    CloseHandle(cleanupThread);
    LogMessage(L"injector", LogLevel::Warning,
               L"event=REMOTE_PATH_DEFERRED_FREE_SCHEDULED pid=%lu address=0x%p",
               pid, remoteAddress);
    return true;
}

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

RemoteThreadWaitResult WaitForRemoteThread(HANDLE thread,
                                           PCWSTR apiName,
                                           DWORD timeoutMs,
                                           std::wstring* error) {
    RemoteThreadWaitResult result{};
    const DWORD wait = WaitForSingleObject(thread, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        if (error) {
            *error = std::wstring(apiName ? apiName : L"remote thread") +
                     L" thread did not finish before timeout; the remote operation may still be running";
        }
        return result;
    }
    if (wait != WAIT_OBJECT_0) {
        if (error) {
            *error = std::wstring(apiName ? apiName : L"remote thread") +
                     L" wait failed: " + FormatLastError(GetLastError());
        }
        return result;
    }

    result.threadFinished = true;
    DWORD threadExitCode = 0;
    if (!GetExitCodeThread(thread, &threadExitCode)) {
        if (error) {
            *error = L"GetExitCodeThread failed: " + FormatLastError(GetLastError());
        }
        return result;
    }

    result.resultAvailable = true;
    result.exitCode = threadExitCode;
    return result;
}

uintptr_t ResolveRemoteDllExport(HMODULE remoteModule,
                                 const std::wstring& dllPath,
                                 const char* exportName,
                                 std::wstring* error) {
    if (!remoteModule || dllPath.empty() || !exportName) {
        if (error) {
            *error = L"invalid remote DLL export resolve arguments";
        }
        return 0;
    }

    HMODULE localModule = LoadLibraryExW(
        dllPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!localModule) {
        if (error) {
            *error = L"LoadLibraryExW(DONT_RESOLVE_DLL_REFERENCES) failed: " +
                     FormatLastError(GetLastError());
        }
        return 0;
    }

    FARPROC localExport = GetProcAddress(localModule, exportName);
    if (!localExport) {
        if (error) {
            *error = L"GetProcAddress failed for hook control export: " +
                     FormatLastError(GetLastError());
        }
        FreeLibrary(localModule);
        return 0;
    }

    const uintptr_t exportRva = reinterpret_cast<uintptr_t>(localExport) -
                                reinterpret_cast<uintptr_t>(localModule);
    FreeLibrary(localModule);
    return reinterpret_cast<uintptr_t>(remoteModule) + exportRva;
}

bool CallRemoteRoutine(HANDLE process,
                       uintptr_t routine,
                       PCWSTR operationName,
                       DWORD timeoutMs,
                       DWORD* exitCode,
                       std::wstring* error) {
    if (exitCode) {
        *exitCode = 0;
    }
    if (!process || routine == 0) {
        if (error) {
            *error = L"invalid remote routine call arguments";
        }
        return false;
    }

    HANDLE thread = CreateRemoteThread(
        process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(routine), nullptr, 0, nullptr);
    if (!thread) {
        if (error) {
            *error = std::wstring(L"CreateRemoteThread(") +
                     (operationName ? operationName : L"hook control") +
                     L") failed: " + FormatLastError(GetLastError());
        }
        return false;
    }

    const RemoteThreadWaitResult wait =
        WaitForRemoteThread(thread, operationName, timeoutMs, error);
    CloseHandle(thread);
    if (!wait.resultAvailable) {
        return false;
    }

    if (exitCode) {
        *exitCode = wait.exitCode;
    }
    if (wait.exitCode != ERROR_SUCCESS) {
        if (error) {
            *error = std::wstring(operationName ? operationName : L"hook control") +
                     L" returned error " + std::to_wstring(wait.exitCode) +
                     L": " + FormatLastError(wait.exitCode);
        }
        return false;
    }
    return true;
}

bool StartHookWithProcess(HANDLE process,
                          const std::wstring& dllPath,
                          HMODULE remoteModule,
                          std::wstring* error) {
    const uintptr_t remoteStart = ResolveRemoteDllExport(
        remoteModule, dllPath, kHookStartExport, error);
    if (!remoteStart) {
        return false;
    }

    LogMessage(L"injector", LogLevel::Info,
               L"event=HOOK_START_BEGIN module=0x%p remoteStart=0x%p",
               remoteModule, reinterpret_cast<void*>(remoteStart));
    DWORD startResult = 0;
    if (!CallRemoteRoutine(process, remoteStart, L"TouchRevHookStart",
                           kHookControlTimeoutMs, &startResult, error)) {
        return false;
    }

    LogMessage(L"injector", LogLevel::Info,
               L"event=HOOK_START_ACCEPTED module=0x%p result=%lu",
               remoteModule, startResult);
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

bool StartHookInProcess(DWORD pid,
                        const std::wstring& dllPath,
                        HMODULE remoteModule,
                        std::wstring* error) {
    if (!remoteModule || dllPath.empty()) {
        if (error) {
            *error = L"hook module or DLL path is empty";
        }
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                     PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        if (error) {
            *error = L"OpenProcess for hook start failed: " +
                     FormatLastError(GetLastError());
        }
        return false;
    }

    const bool started = StartHookWithProcess(process, dllPath, remoteModule, error);
    CloseHandle(process);
    return started;
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

    const RemoteThreadWaitResult loadWait = WaitForRemoteThread(
        thread, L"remote LoadLibraryW", kLoadLibraryTimeoutMs, error);

    if (loadWait.threadFinished) {
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        CloseHandle(thread);
    } else {
        if (ScheduleDeferredRemoteFree(process, thread, remotePath, pid)) {
            process = nullptr;
            thread = nullptr;
            remotePath = nullptr;
            if (error) {
                *error += L"; the remote DLL path will be released after the remote thread actually finishes";
            }
        } else {
            CloseHandle(thread);
            LogMessage(L"injector", LogLevel::Warning,
                       L"event=REMOTE_PATH_RETAINED pid=%lu address=0x%p bytes=%zu reason=deferred-cleanup-thread-create-failed",
                       pid, remotePath, bytes);
            if (error) {
                *error += L"; the remote DLL path allocation was retained because safe deferred cleanup could not be scheduled";
            }
        }
    }

    if (!loadWait.resultAvailable) {
        if (process) {
            CloseHandle(process);
        }
        return false;
    }

    const DWORD exitCode = loadWait.exitCode;

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

    if (!StartHookWithProcess(process, dllPath, moduleInfo.module, error)) {
        CloseHandle(process);
        return false;
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
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                     PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        if (error) {
            *error = L"OpenProcess for uninstall failed: " +
                     FormatLastError(GetLastError());
        }
        return false;
    }

    const uintptr_t remoteStop = ResolveRemoteDllExport(
        moduleInfo.module, dllPath, kHookStopExport, error);
    if (!remoteStop) {
        CloseHandle(process);
        return false;
    }

    LogMessage(L"injector", LogLevel::Info,
               L"event=HOOK_STOP_BEGIN pid=%lu module=0x%p remoteStop=0x%p",
               pid, moduleInfo.module, reinterpret_cast<void*>(remoteStop));
    DWORD stopResult = 0;
    if (!CallRemoteRoutine(process, remoteStop, L"TouchRevHookStop",
                           kHookControlTimeoutMs, &stopResult, error)) {
        CloseHandle(process);
        return false;
    }

    LogMessage(L"injector", LogLevel::Info,
               L"event=HOOK_STOP_DONE pid=%lu module=0x%p result=%lu",
               pid, moduleInfo.module, stopResult);

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

    const RemoteThreadWaitResult freeWait = WaitForRemoteThread(
        thread, L"remote FreeLibrary", kLoadLibraryTimeoutMs, error);
    CloseHandle(thread);
    CloseHandle(process);

    if (!freeWait.resultAvailable) {
        return false;
    }

    const DWORD exitCode = freeWait.exitCode;

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
