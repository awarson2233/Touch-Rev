#include "common/log.h"
#include "common/winutil.h"
#include "injector/inject.h"
#include "injector/process_find.h"

#include <windows.h>

#include <iostream>
#include <string>

#if !defined(TOUCHREV_ARCH_ARM64) && !defined(TOUCHREV_ARCH_X64)
#error TouchRevInjector.exe must be built as ARM64 or x64.
#endif

namespace {

constexpr PCWSTR kDefaultHookDllName = L"TouchRevHook.dll";

enum class Command {
    None,
    Install,
    Status,
    Uninstall,
};

struct CommandLine {
    Command command{Command::None};
    std::wstring dllPath;
};

void PrintUsage() {
    std::wcout << L"Usage:\n"
               << L"  TouchRevInjector.exe --install [--dll <path>]\n"
               << L"  TouchRevInjector.exe --status [--dll <path>]\n"
               << L"  TouchRevInjector.exe --uninstall [--dll <path>]\n"
               << L"\n"
               << L"Default DLL path: <TouchRevInjector.exe directory>\\"
               << kDefaultHookDllName << L"\n";
}

bool SetCommand(CommandLine* commandLine, Command command) {
    if (!commandLine || commandLine->command != Command::None) {
        return false;
    }

    commandLine->command = command;
    return true;
}

bool ParseCommandLine(int argc, wchar_t** argv, CommandLine* commandLine) {
    if (!commandLine) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--install") {
            if (!SetCommand(commandLine, Command::Install)) {
                std::wcerr << L"Specify exactly one command.\n";
                return false;
            }
        } else if (arg == L"--status") {
            if (!SetCommand(commandLine, Command::Status)) {
                std::wcerr << L"Specify exactly one command.\n";
                return false;
            }
        } else if (arg == L"--uninstall") {
            if (!SetCommand(commandLine, Command::Uninstall)) {
                std::wcerr << L"Specify exactly one command.\n";
                return false;
            }
        } else if (arg == L"--dll" && i + 1 < argc) {
            commandLine->dllPath = argv[++i];
        } else if (arg == L"--help" || arg == L"-h") {
            return false;
        } else {
            std::wcerr << L"Unknown argument: " << arg << L"\n";
            return false;
        }
    }

    return commandLine->command != Command::None;
}

std::wstring GetExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');

    for (;;) {
        DWORD written = GetModuleFileNameW(nullptr, path.data(),
                                           static_cast<DWORD>(path.size()));
        if (written == 0) {
            return {};
        }

        if (written < path.size() - 1) {
            path.resize(written);
            return path;
        }

        path.resize(path.size() * 2);
    }
}

std::wstring GetDefaultDllPath() {
    std::wstring exePath = GetExecutablePath();
    if (exePath.empty()) {
        return {};
    }

    size_t slash = exePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return kDefaultHookDllName;
    }

    return exePath.substr(0, slash + 1) + kDefaultHookDllName;
}

std::wstring ResolveDllPath(const CommandLine& commandLine) {
    if (!commandLine.dllPath.empty()) {
        return touchrev::GetFullPath(commandLine.dllPath);
    }

    return GetDefaultDllPath();
}

int Fail(const std::wstring& message) {
    touchrev::LogMessage(L"injector", touchrev::LogLevel::Error,
                         L"event=FAILED message=%s", message.c_str());
    std::wcerr << L"ERROR: " << message << L"\n";
    std::wcerr << L"Log: " << touchrev::GetLogPath() << L"\n";
    touchrev::ShutdownLog();
    return 1;
}

void PrintTarget(const touchrev::TargetProcess& target) {
    std::wcout << L"Find Shell_TrayWnd OK: hwnd=0x" << std::hex
               << reinterpret_cast<uintptr_t>(target.shellTrayHwnd) << std::dec
               << L"\n";
    std::wcout << L"Target process: pid=" << target.pid
               << L" image=\"" << target.imagePath << L"\"\n";
    std::wcout << L"Expected machine: "
               << touchrev::CurrentBuildArchName() << L"\n";
    std::wcout << L"Target processMachine: "
               << touchrev::MachineTypeToString(target.processMachine) << L"\n";
    std::wcout << L"Target nativeMachine: "
               << touchrev::MachineTypeToString(target.nativeMachine) << L"\n";
}

void PrintModuleStatus(const touchrev::RemoteModuleInfo& moduleInfo) {
    if (!moduleInfo.module) {
        std::wcout << L"Status: not loaded\n";
        return;
    }

    std::wcout << L"Status: loaded\n";
    std::wcout << L"Remote module: 0x" << std::hex
               << reinterpret_cast<uintptr_t>(moduleInfo.module) << std::dec
               << L"\n";
    std::wcout << L"Module path: " << moduleInfo.modulePath << L"\n";
    std::wcout << L"Match: "
               << (moduleInfo.matchedByFullPath ? L"full-path" : L"basename")
               << L"\n";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    touchrev::InitializeLog(L"injector");
    touchrev::LogMessage(L"injector", touchrev::LogLevel::Info,
                         L"event=INIT arch=%s", touchrev::CurrentBuildArchName());

    CommandLine commandLine;
    if (!ParseCommandLine(argc, argv, &commandLine)) {
        PrintUsage();
        touchrev::ShutdownLog();
        return 2;
    }

    std::wstring fullDllPath = ResolveDllPath(commandLine);
    if (fullDllPath.empty()) {
        return Fail(L"failed to resolve DLL path");
    }

    std::wcout << L"DLL path: " << fullDllPath << L"\n";

    DWORD attributes = GetFileAttributesW(fullDllPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return Fail(L"DLL file does not exist: " + fullDllPath);
    }

    USHORT dllMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    if (!touchrev::GetPeMachineType(fullDllPath, &dllMachine)) {
        return Fail(L"failed to read DLL PE machine type: " + fullDllPath);
    }

    if (dllMachine != touchrev::CurrentBuildMachineType()) {
        return Fail(L"DLL machine does not match injector architecture: expected=" +
                    std::wstring(touchrev::CurrentBuildArchName()) +
                    L" actual=" + touchrev::MachineTypeToString(dllMachine));
    }

    std::wstring error;
    if (!touchrev::IsCurrentProcessNativeForCurrentBuild(&error)) {
        return Fail(error);
    }

    touchrev::TargetProcess target;
    if (!touchrev::FindShellExplorer(&target, &error)) {
        return Fail(error);
    }

    if (!touchrev::IsNativeMachineForCurrentBuild(target.processMachine,
                                                  target.nativeMachine)) {
        return Fail(L"target explorer.exe architecture does not match injector architecture: expected=" +
                    std::wstring(touchrev::CurrentBuildArchName()) +
                    L" processMachine=" +
                    touchrev::MachineTypeToString(target.processMachine) +
                    L" nativeMachine=" +
                    touchrev::MachineTypeToString(target.nativeMachine));
    }

    PrintTarget(target);

    touchrev::RemoteModuleInfo moduleInfo;
    if (!touchrev::QueryRemoteDllModule(target.pid, fullDllPath, &moduleInfo,
                                        &error)) {
        return Fail(error);
    }

    switch (commandLine.command) {
    case Command::Status:
        PrintModuleStatus(moduleInfo);
        touchrev::LogMessage(L"injector", touchrev::LogLevel::Info,
                             L"event=STATUS loaded=%d module=0x%p modulePath=%s",
                             moduleInfo.module != nullptr, moduleInfo.module,
                             moduleInfo.modulePath.c_str());
        break;

    case Command::Install: {
        if (moduleInfo.module) {
            std::wcout << L"Already loaded; install skipped.\n";
            PrintModuleStatus(moduleInfo);
            touchrev::LogMessage(L"injector", touchrev::LogLevel::Info,
                                 L"event=INSTALL_SKIPPED reason=already-loaded "
                                 L"module=0x%p modulePath=%s",
                                 moduleInfo.module, moduleInfo.modulePath.c_str());
            break;
        }

        HMODULE remoteModule = nullptr;
        if (!touchrev::InjectDllIntoProcess(target.pid, fullDllPath, &remoteModule,
                                            &error)) {
            return Fail(error);
        }

        std::wcout << L"Remote LoadLibraryW result != 0\n";
        std::wcout << L"Remote module: 0x" << std::hex
                   << reinterpret_cast<uintptr_t>(remoteModule) << std::dec
                   << L"\n";
        touchrev::LogMessage(L"injector", touchrev::LogLevel::Info,
                             L"event=DONE command=install remoteModule=0x%p",
                             remoteModule);
        break;
    }

    case Command::Uninstall: {
        if (!moduleInfo.module) {
            std::wcout << L"Status: not loaded\n";
            std::wcout << L"Uninstall skipped; module is not loaded.\n";
            touchrev::LogMessage(L"injector", touchrev::LogLevel::Info,
                                 L"event=UNINSTALL_SKIPPED reason=not-loaded");
            break;
        }

        touchrev::RemoteModuleInfo unloadedModule;
        DWORD freeLibraryResult = 0;
        if (!touchrev::UninstallDllFromProcess(target.pid, fullDllPath,
                                               &unloadedModule,
                                               &freeLibraryResult, &error)) {
            return Fail(error);
        }

        if (!unloadedModule.module) {
            std::wcout << L"Status: not loaded\n";
            std::wcout << L"Uninstall skipped; module is not loaded.\n";
            break;
        }

        std::wcout << L"Uninstalled module: 0x" << std::hex
                   << reinterpret_cast<uintptr_t>(unloadedModule.module) << std::dec
                   << L"\n";
        std::wcout << L"FreeLibrary result: " << freeLibraryResult << L"\n";
        std::wcout << L"Status: not loaded\n";
        touchrev::LogMessage(L"injector", touchrev::LogLevel::Info,
                             L"event=DONE command=uninstall module=0x%p "
                             L"freeLibraryResult=%lu",
                             unloadedModule.module, freeLibraryResult);
        break;
    }

    case Command::None:
    default:
        touchrev::ShutdownLog();
        return 2;
    }

    std::wcout << L"Log: " << touchrev::GetLogPath() << L"\n";
    touchrev::ShutdownLog();
    return 0;
}
