#pragma once

#include <windows.h>
#include <string>

namespace touchrev::common
{
inline std::wstring ModuleRelativePath(const std::wstring& relativePath)
{
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    std::wstring path(modulePath, modulePath + length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        path.resize(slash + 1);
    }
    path += relativePath;
    return path;
}
}
