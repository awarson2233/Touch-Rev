#include "PdbCache.h"

#include "RegistryUtils.h"

#include <shlwapi.h>
#include <strsafe.h>

#include <algorithm>
#include <iterator>
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

TrimResult Trim()
{
    struct Version
    {
        std::wstring path;
        FILETIME lastWrite{};
        unsigned long long bytes = 0;
        bool newest = false;
    };

    TrimResult result{};
    const std::wstring root = Root();
    if (root.empty())
    {
        return result;
    }

    const auto listDirectories = [](const std::wstring& directory) {
        std::vector<WIN32_FIND_DATAW> entries;
        WIN32_FIND_DATAW findData{};
        const HANDLE find = FindFirstFileExW((directory + L"\\*").c_str(), FindExInfoBasic,
                                             &findData, FindExSearchNameMatch, nullptr, 0);
        if (find == INVALID_HANDLE_VALUE)
        {
            return entries;
        }
        do
        {
            const bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                                     (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
            if (isDirectory && wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0)
            {
                entries.push_back(findData);
            }
        } while (FindNextFileW(find, &findData));
        FindClose(find);
        return entries;
    };
    const auto newerFirst = [](const Version& a, const Version& b) {
        return CompareFileTime(&a.lastWrite, &b.lastWrite) > 0;
    };

    std::vector<Version> candidates;
    unsigned long long totalBytes = 0;
    for (const WIN32_FIND_DATAW& pdbEntry : listDirectories(root))
    {
        const std::wstring pdbDirectory = root + L"\\" + pdbEntry.cFileName;
        std::vector<Version> versions;
        for (const WIN32_FIND_DATAW& versionEntry : listDirectories(pdbDirectory))
        {
            Version version{.path = pdbDirectory + L"\\" + versionEntry.cFileName, .lastWrite = versionEntry.ftLastWriteTime};
            Usage usage{};
            Walk(version.path, usage);
            version.bytes = usage.totalBytes;
            totalBytes += version.bytes;
            versions.push_back(std::move(version));
        }
        if (versions.empty())
        {
            continue;
        }

        std::sort(versions.begin(), versions.end(), newerFirst);
        versions.front().newest = true;
        candidates.insert(candidates.end(), std::make_move_iterator(versions.begin()), std::make_move_iterator(versions.end()));
    }

    std::sort(candidates.begin(), candidates.end(), newerFirst);
    std::vector<bool> remove(candidates.size(), false);

    // 先按每个 PDB 的版本数裁剪：candidates 已按新到旧排序，逐个累计同目录版本数。
    std::vector<std::pair<std::wstring, unsigned int>> keptPerPdb;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        const std::wstring parent = candidates[i].path.substr(0, candidates[i].path.find_last_of(L'\\'));
        auto kept = std::find_if(keptPerPdb.begin(), keptPerPdb.end(), [&parent](const auto& item) {
            return item.first == parent;
        });
        if (kept == keptPerPdb.end())
        {
            keptPerPdb.emplace_back(parent, 0u);
            kept = std::prev(keptPerPdb.end());
        }
        if (kept->second >= kMaxVersionsPerPdb && !candidates[i].newest)
        {
            remove[i] = true;
            totalBytes -= candidates[i].bytes;
            continue;
        }
        ++kept->second;
    }

    // 再按总量从最旧的版本删起。
    for (size_t i = candidates.size(); i > 0 && totalBytes > kMaxCacheBytes; --i)
    {
        if (!remove[i - 1] && !candidates[i - 1].newest)
        {
            remove[i - 1] = true;
            totalBytes -= candidates[i - 1].bytes;
        }
    }

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (!remove[i])
        {
            continue;
        }

        ClearResult clear{};
        if (ClearDirectory(candidates[i].path, clear) && RemoveDirectoryW(candidates[i].path.c_str()))
        {
            ++result.removedVersions;
            result.freedBytes += candidates[i].bytes;
        }
    }
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
