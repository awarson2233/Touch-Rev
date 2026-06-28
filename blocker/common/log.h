#pragma once

#include <windows.h>

namespace touchrev {

enum class LogLevel {
    Info,
    Warning,
    Error,
};

void InitializeLog(PCWSTR component);
void ShutdownLog();
void LogMessage(PCWSTR component, LogLevel level, PCWSTR format, ...);

}  // namespace touchrev
