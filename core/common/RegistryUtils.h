#pragma once

#include <windows.h>

#include <string>

namespace touchrev::common::registry
{
inline DWORD ReadDword(HKEY root, PCWSTR subKey, PCWSTR name, DWORD fallback)
{
    DWORD value = fallback;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegGetValueW(root, subKey, name, RRF_RT_REG_DWORD, &type, &value, &size) != ERROR_SUCCESS)
    {
        return fallback;
    }
    return value;
}

inline bool WriteDword(HKEY root, PCWSTR subKey, PCWSTR name, DWORD value)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    const LSTATUS status = RegSetValueExW(
        key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

inline std::wstring ReadString(HKEY root, PCWSTR subKey, PCWSTR name)
{
    wchar_t buffer[1024]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, subKey, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                     nullptr, buffer, &size) != ERROR_SUCCESS)
    {
        return {};
    }
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    return buffer;
}

inline bool WriteString(HKEY root, PCWSTR subKey, PCWSTR name, const std::wstring& value)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    const LSTATUS status = RegSetValueExW(
        key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

inline bool DeleteValue(HKEY root, PCWSTR subKey, PCWSTR name)
{
    const LSTATUS status = RegDeleteKeyValueW(root, subKey, name);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}
}
