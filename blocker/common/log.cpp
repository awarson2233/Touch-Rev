#include "common/log.h"

#include <strsafe.h>

#include <string>

namespace touchrev {
namespace {

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

}  // namespace

void InitializeLog(PCWSTR component) {
    if (component) {
        OutputDebugStringW((std::wstring(L"TouchRevHook log initialized for ") +
                            component + L"\r\n")
                               .c_str());
    }
}

void ShutdownLog() {
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
}

}  // namespace touchrev

