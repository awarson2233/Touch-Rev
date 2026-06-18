#include "injector/process_find.h"

#include "common/constants.h"
#include "common/log.h"
#include "common/winutil.h"

namespace touchrev {
namespace {

using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE process,
                                        USHORT* processMachine,
                                        USHORT* nativeMachine);

std::wstring QueryProcessImagePath(HANDLE process) {
    std::wstring imagePath(MAX_PATH, L'\0');
    DWORD chars = static_cast<DWORD>(imagePath.size());

    while (!QueryFullProcessImageNameW(process, 0, imagePath.data(), &chars)) {
        DWORD error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER) {
            return {};
        }

        imagePath.resize(imagePath.size() * 2);
        chars = static_cast<DWORD>(imagePath.size());
    }

    imagePath.resize(chars);
    return imagePath;
}

}  // namespace

bool QueryProcessMachine(HANDLE process,
                         USHORT* processMachine,
                         USHORT* nativeMachine,
                         std::wstring* error) {
    if (!processMachine || !nativeMachine) {
        if (error) {
            *error = L"invalid machine output pointer";
        }
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto isWow64Process2 = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(kernel32, "IsWow64Process2"));
    if (!isWow64Process2) {
        if (error) {
            *error = L"IsWow64Process2 is unavailable";
        }
        return false;
    }

    USHORT queriedProcessMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT queriedNativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    if (!isWow64Process2(process, &queriedProcessMachine,
                         &queriedNativeMachine)) {
        if (error) {
            *error = L"IsWow64Process2 failed: " + FormatLastError(GetLastError());
        }
        return false;
    }

    *processMachine = queriedProcessMachine;
    *nativeMachine = queriedNativeMachine;
    return true;
}

bool IsNativeMachineForCurrentBuild(USHORT processMachine, USHORT nativeMachine) {
    return processMachine == IMAGE_FILE_MACHINE_UNKNOWN &&
           nativeMachine == CurrentBuildMachineType();
}

bool IsCurrentProcessNativeForCurrentBuild(std::wstring* error) {
    USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    if (!QueryProcessMachine(GetCurrentProcess(), &processMachine, &nativeMachine,
                             error)) {
        return false;
    }

    if (!IsNativeMachineForCurrentBuild(processMachine, nativeMachine)) {
        if (error) {
            *error = L"current process architecture does not match build architecture: expected=" +
                     std::wstring(CurrentBuildArchName()) +
                     L" processMachine=" + MachineTypeToString(processMachine) +
                     L" nativeMachine=" + MachineTypeToString(nativeMachine);
        }
        return false;
    }

    return true;
}

bool FindShellExplorer(TargetProcess* target, std::wstring* error) {
    if (!target) {
        if (error) {
            *error = L"invalid target output pointer";
        }
        return false;
    }

    HWND shellTray = FindWindowW(kClassShellTrayWnd, nullptr);
    if (!shellTray) {
        if (error) {
            *error = L"FindWindowW(Shell_TrayWnd) failed: " +
                     FormatLastError(GetLastError());
        }
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(shellTray, &pid);
    if (pid == 0) {
        if (error) {
            *error = L"GetWindowThreadProcessId returned pid=0";
        }
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        if (error) {
            *error = L"OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) failed: " +
                     FormatLastError(GetLastError());
        }
        return false;
    }

    std::wstring imagePath = QueryProcessImagePath(process);
    if (imagePath.empty()) {
        if (error) {
            *error = L"QueryFullProcessImageNameW failed: " +
                     FormatLastError(GetLastError());
        }
        CloseHandle(process);
        return false;
    }

    std::wstring imageName = GetFileNameFromPath(imagePath);
    if (!EqualsIgnoreCase(imageName, L"explorer.exe")) {
        if (error) {
            *error = L"Shell_TrayWnd owner is not explorer.exe: " + imagePath;
        }
        CloseHandle(process);
        return false;
    }

    USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    if (!QueryProcessMachine(process, &processMachine, &nativeMachine, error)) {
        CloseHandle(process);
        return false;
    }

    CloseHandle(process);

    target->shellTrayHwnd = shellTray;
    target->pid = pid;
    target->imagePath = imagePath;
    target->processMachine = processMachine;
    target->nativeMachine = nativeMachine;

    LogMessage(L"injector", LogLevel::Info,
               L"event=TARGET_PROCESS pid=%lu hwnd=0x%p image=%s "
               L"processMachine=%s nativeMachine=%s",
               pid, shellTray, imagePath.c_str(),
               MachineTypeToString(processMachine), MachineTypeToString(nativeMachine));
    return true;
}

}  // namespace touchrev
