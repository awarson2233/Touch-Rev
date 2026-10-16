#include "Elevation.h"

#include <shellapi.h>

namespace touchrev::common::elevation
{
bool IsElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

bool RelaunchElevated(const std::wstring& arguments, DWORD* error)
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = path;
    info.lpParameters = arguments.c_str();
    info.nShow = SW_SHOWNORMAL;
    const bool launched = ShellExecuteExW(&info) != FALSE;
    if (error)
    {
        *error = launched ? ERROR_SUCCESS : GetLastError();
    }
    return launched;
}
}
