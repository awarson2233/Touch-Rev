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

constexpr ULONGLONG kRepeatWindowMs = 10000;
constexpr size_t kMaxTrackedCallSites = 64;

struct RecentCallSite {
    PCWSTR format = nullptr;
    ULONGLONG lastSent = 0;
    unsigned suppressed = 0;
};

SRWLOCK g_recentLock = SRWLOCK_INIT;
RecentCallSite g_recentCallSites[kMaxTrackedCallSites];

// 以格式串地址标识调用点：同一调用点在窗口期内只放行第一次，放行时返回期间被抑制的次数。
bool ShouldSend(PCWSTR format, unsigned* suppressed) {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_recentLock);

    RecentCallSite* slot = nullptr;
    RecentCallSite* oldest = &g_recentCallSites[0];
    for (auto& entry : g_recentCallSites) {
        if (entry.format == format) {
            slot = &entry;
            break;
        }
        if (entry.lastSent < oldest->lastSent) {
            oldest = &entry;
        }
    }

    bool send = true;
    *suppressed = 0;
    if (slot && now - slot->lastSent < kRepeatWindowMs) {
        ++slot->suppressed;
        send = false;
    } else if (slot) {
        *suppressed = slot->suppressed;
        slot->suppressed = 0;
        slot->lastSent = now;
    } else {
        // 新调用点占用空槽或最久未发送的槽，内存占用固定。
        *oldest = {format, now, 0};
    }

    ReleaseSRWLockExclusive(&g_recentLock);
    return send;
}

}  // namespace

void InitializeLog(PCWSTR component) {
#if defined(TOUCHREV_TRACE)
    if (component) {
        OutputDebugStringW((std::wstring(L"TouchRevHook log initialized for ") +
                            component + L"\r\n")
                               .c_str());
    }
#else
    UNREFERENCED_PARAMETER(component);
#endif
}

void ShutdownLog() {
}

void LogMessage(PCWSTR component, LogLevel level, PCWSTR format, ...) {
    // Info 是追踪日志，只在 Debug 构建输出；Warning/Error 所有构建保留，但按调用点限频。
#if !defined(TOUCHREV_TRACE)
    if (level == LogLevel::Info) {
        return;
    }
#endif
    unsigned suppressed = 0;
    if (level != LogLevel::Info && !ShouldSend(format, &suppressed)) {
        return;
    }

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

    std::wstring line = std::wstring(prefix) + message;
    if (suppressed > 0) {
        line += L" (suppressed " + std::to_wstring(suppressed) + L" repeats)";
    }
    line += L"\r\n";
    OutputDebugStringW(line.c_str());
}

}  // namespace touchrev

