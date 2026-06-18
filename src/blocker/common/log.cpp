#include "common/log.h"

#include <strsafe.h>

#include <mutex>
#include <string>
#include <vector>

namespace touchrev {
namespace {

std::mutex g_logMutex;
std::wstring g_logPath;

PCWSTR LevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::Info:
        return L"INFO";
    case LogLevel::Warning:
        return L"WARN";
    case LogLevel::Error:
        return L"ERROR";
    }

    return L"INFO";
}

std::wstring GetEnvironmentString(PCWSTR name) {
    DWORD chars = GetEnvironmentVariableW(name, nullptr, 0);
    if (chars == 0) {
        return {};
    }

    std::wstring value(chars, L'\0');
    DWORD written = GetEnvironmentVariableW(name, value.data(), chars);
    if (written == 0 || written >= chars) {
        return {};
    }

    value.resize(written);
    return value;
}

std::wstring BuildDefaultLogPath() {
    std::wstring base = GetEnvironmentString(L"LOCALAPPDATA");
    if (base.empty()) {
        wchar_t tempPath[MAX_PATH];
        DWORD chars = GetTempPathW(ARRAYSIZE(tempPath), tempPath);
        if (chars == 0 || chars >= ARRAYSIZE(tempPath)) {
            base = L".";
        } else {
            base.assign(tempPath, chars);
            while (!base.empty() && (base.back() == L'\\' || base.back() == L'/')) {
                base.pop_back();
            }
        }
    }

    std::wstring dir = base + L"\\TouchRevHook";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\TouchRevHook.log";
}

const std::wstring& EnsureLogPath() {
    if (g_logPath.empty()) {
        g_logPath = BuildDefaultLogPath();
    }

    return g_logPath;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                    static_cast<int>(text.size()), nullptr, 0,
                                    nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }

    std::string result(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), bytes, nullptr, nullptr);
    return result;
}

void AppendUtf8LineToFile(const std::wstring& path, const std::wstring& line) {
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    std::string utf8 = WideToUtf8(line);
    if (!utf8.empty()) {
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
                  nullptr);
    }

    CloseHandle(file);
}

}  // namespace

void InitializeLog(PCWSTR component) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    EnsureLogPath();

    if (component) {
        OutputDebugStringW((std::wstring(L"TouchRevHook log initialized for ") +
                            component + L"\r\n")
                               .c_str());
    }
}

void ShutdownLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logPath.clear();
}

std::wstring GetLogPath() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return EnsureLogPath();
}

void LogMessage(PCWSTR component, LogLevel level, PCWSTR format, ...) {
    wchar_t message[2048];
    message[0] = L'\0';

    va_list args;
    va_start(args, format);
    StringCchVPrintfW(message, ARRAYSIZE(message), format ? format : L"", args);
    va_end(args);

    SYSTEMTIME localTime;
    GetLocalTime(&localTime);

    wchar_t prefix[512];
    StringCchPrintfW(prefix, ARRAYSIZE(prefix),
                     L"%04u-%02u-%02uT%02u:%02u:%02u.%03u pid=%lu tid=%lu "
                     L"component=%s level=%s ",
                     localTime.wYear, localTime.wMonth, localTime.wDay,
                     localTime.wHour, localTime.wMinute, localTime.wSecond,
                     localTime.wMilliseconds, GetCurrentProcessId(),
                     GetCurrentThreadId(), component ? component : L"unknown",
                     LevelToString(level));

    std::wstring line = std::wstring(prefix) + message + L"\r\n";
    OutputDebugStringW(line.c_str());

    std::lock_guard<std::mutex> lock(g_logMutex);
    AppendUtf8LineToFile(EnsureLogPath(), line);
}

}  // namespace touchrev
