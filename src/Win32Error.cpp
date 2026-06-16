#include "Win32Error.h"

#include <iomanip>
#include <sstream>

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

void DebugLog(std::wstring_view message)
{
    std::wstring line(message);
    line += L"\n";
    OutputDebugStringW(line.c_str());
}

void DebugLogHResult(std::wstring_view context, HRESULT hr)
{
    std::wstring line(context);
    line += L" failed: ";
    line += HResultToHex(hr);
    line += L" ";
    line += HResultToMessage(hr);
    DebugLog(line);
}
