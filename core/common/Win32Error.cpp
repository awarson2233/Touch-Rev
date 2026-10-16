#include "Win32Error.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

std::wstring HResultToHex(HRESULT hr)
{
    std::wstringstream stream;
    stream << L"0x" << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
           << static_cast<unsigned long>(hr);
    return stream.str();
}

std::wstring HResultToMessage(HRESULT hr)
{
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(
        flags,
        nullptr,
        static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message;
    if (length != 0 && buffer != nullptr)
    {
        message.assign(buffer, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
        {
            message.pop_back();
        }
    }

    if (buffer != nullptr)
    {
        LocalFree(buffer);
    }

    return message.empty() ? L"Unknown error" : message;
}

HRESULT HResultFromLastError()
{
    const DWORD error = GetLastError();
    return error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(error);
}

namespace
{
constexpr ULONGLONG kRepeatWindowMs = 10000;
constexpr size_t kMaxTrackedMessages = 64;

struct RecentMessage
{
    size_t hash = 0;
    ULONGLONG lastSent = 0;
    unsigned suppressed = 0;
};

std::mutex g_recentMutex;
std::vector<RecentMessage> g_recentMessages;

// 同一键在窗口期内只放行第一次；放行时通过 suppressed 返回上次放行以来被抑制的次数。
bool ShouldSend(std::wstring_view key, unsigned& suppressed)
{
    const size_t hash = std::hash<std::wstring_view>{}(key);
    const ULONGLONG now = GetTickCount64();
    std::lock_guard lock(g_recentMutex);

    for (auto& entry : g_recentMessages)
    {
        if (entry.hash != hash)
        {
            continue;
        }
        if (now - entry.lastSent < kRepeatWindowMs)
        {
            ++entry.suppressed;
            return false;
        }
        suppressed = std::exchange(entry.suppressed, 0u);
        entry.lastSent = now;
        return true;
    }

    // 表满时淘汰最久未发送的一项，内存占用固定。
    if (g_recentMessages.size() >= kMaxTrackedMessages)
    {
        auto oldest = std::min_element(
            g_recentMessages.begin(), g_recentMessages.end(), [](const RecentMessage& a, const RecentMessage& b) {
                return a.lastSent < b.lastSent;
            });
        g_recentMessages.erase(oldest);
    }
    g_recentMessages.push_back({hash, now, 0});
    suppressed = 0;
    return true;
}

void Send(std::wstring line, unsigned suppressed)
{
    if (suppressed > 0)
    {
        line += L" (suppressed ";
        line += std::to_wstring(suppressed);
        line += L" repeats)";
    }
    line += L"\n";
    OutputDebugStringW(line.c_str());
}
}

void DebugLog(std::wstring_view message)
{
    unsigned suppressed = 0;
    if (ShouldSend(message, suppressed))
    {
        Send(std::wstring(message), suppressed);
    }
}

void DebugLogHResult(std::wstring_view context, HRESULT hr)
{
    // 先按"上下文 + 错误码"判定限频，被抑制时连 FormatMessage 都不做。
    std::wstring line(context);
    line += L" failed: ";
    line += HResultToHex(hr);

    unsigned suppressed = 0;
    if (!ShouldSend(line, suppressed))
    {
        return;
    }
    line += L" ";
    line += HResultToMessage(hr);
    Send(std::move(line), suppressed);
}

void DebugTrace(std::wstring_view message)
{
    std::wstring line(message);
    line += L"\n";
    OutputDebugStringW(line.c_str());
}
