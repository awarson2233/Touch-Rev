#pragma once

#include <windows.h>

#include <string>

namespace touchrev {

enum class LogLevel {
    Info,
    Warning,
    Error,
};

void InitializeLog(PCWSTR component);
void ShutdownLog();
std::wstring GetLogPath();
void LogMessage(PCWSTR component, LogLevel level, PCWSTR format, ...);

}  // namespace touchrev
