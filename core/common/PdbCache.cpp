#include "PdbCache.h"

#include "RegistryUtils.h"

#include <shlwapi.h>
#include <strsafe.h>

#include <vector>

#pragma comment(lib, "shlwapi.lib")

namespace touchrev::common::pdbcache
{
namespace
{
constexpr wchar_t kStatusKey[] = L"Software\\Touch-Rev";

std::wstring EnvironmentString(PCWSTR name)
{
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0)
    {
        return {};
    }

    std::wstring value(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), length);
    value.resize(written);
    return value;
}

void Walk(const std::wstring& directory, Usage& usage)
{
    WIN32_FIND_DATAW findData{};
    const HANDLE find = FindFirstFileExW((directory + L"\\*").c_str(), FindExInfoBasic,
                                         &findData, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
        {
            continue;
        }

        const std::wstring child = directory + L"\\" + findData.cFileName;
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
            {
                Walk(child, usage);
            }
            continue;
        }

        usage.totalBytes += (static_cast<unsigned long long>(findData.nFileSizeHigh) << 32) |
                            findData.nFileSizeLow;
        ++usage.fileCount;
    } while (FindNextFileW(find, &findData));

    FindClose(find);
}

// 返回 true 表示目录已清空（可被删除）；被占用的文件登记为重启后删除并计入 lockedFiles。
bool ClearDirectory(const std::wstring& directory, ClearResult& result)
{
    WIN32_FIND_DATAW findData{};
    const HANDLE find = FindFirstFileExW((directory + L"\\*").c_str(), FindExInfoBasic,
                                         &findData, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        {
            return true;
        }
        result.lastError = error;
        return false;
    }

    bool emptied = true;
    do
    {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
        {
            continue;
        }

        const std::wstring child = directory + L"\\" + findData.cFileName;
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                emptied = RemoveDirectoryW(child.c_str()) != FALSE && emptied;
                continue;
            }

            if (ClearDirectory(child, result))
            {
                if (!RemoveDirectoryW(child.c_str()))
                {
                    result.lastError = GetLastError();
                    emptied = false;
                }
            }
            else
            {
                emptied = false;
            }
            continue;
        }

        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
        {
            SetFileAttributesW(child.c_str(), findData.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
        }

        if (DeleteFileW(child.c_str()))
        {
            ++result.deletedFiles;
            continue;
        }

        // 拦截仍在运行时 explorer 内的 dbghelp 可能持有 PDB 句柄，退化为重启后删除。
        result.lastError = GetLastError();
        MoveFileExW(child.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        ++result.lockedFiles;
        emptied = false;
    } while (FindNextFileW(find, &findData));

    FindClose(find);
    return emptied;
}
}

std::wstring Root()
{
    const std::wstring localAppData = EnvironmentString(L"LOCALAPPDATA");
    if (localAppData.empty())
    {
        return {};
    }
    return localAppData + L"\\Touch-Rev\\symbol";
}

Usage QueryUsage()
{
    Usage usage{};
    const std::wstring root = Root();
    if (root.empty())
    {
        return usage;
    }

    const DWORD attributes = GetFileAttributesW(root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        return usage;
    }

    usage.exists = true;
    Walk(root, usage);
    return usage;
}

ClearResult Clear()
{
    ClearResult result{};
    const std::wstring root = Root();
    if (root.empty())
    {
        result.lastError = ERROR_PATH_NOT_FOUND;
        return result;
    }

    const DWORD attributes = GetFileAttributesW(root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        result.succeeded = true;
        return result;
    }

    ClearDirectory(root, result);
    result.succeeded = result.lockedFiles == 0;
    return result;
}

void ResetStatus()
{
    registry::WriteDword(HKEY_CURRENT_USER, kStatusKey, L"PdbStatus", 0);
    registry::WriteDword(HKEY_CURRENT_USER, kStatusKey, L"PdbSource", 0);
    registry::WriteDword(HKEY_CURRENT_USER, kStatusKey, L"PdbLastError", ERROR_SUCCESS);
}

std::wstring FormatBytes(unsigned long long bytes)
{
    wchar_t buffer[64]{};
    if (SUCCEEDED(StrFormatByteSizeEx(bytes, SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT,
                                      buffer, ARRAYSIZE(buffer))))
    {
        return buffer;
    }

    StringCchPrintfW(buffer, ARRAYSIZE(buffer), L"%llu 字节", bytes);
    return buffer;
}
}
