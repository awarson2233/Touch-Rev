#pragma once

#include <windows.h>

#include <string>

namespace touchrev::common::pdbcache
{
// 缓存根目录：%LOCALAPPDATA%\Touch-Rev\symbol，与 hookdll 的 BuildSymbolCacheRoot 保持一致。
std::wstring Root();

struct Usage
{
    bool exists = false;
    unsigned long long totalBytes = 0;
    unsigned int fileCount = 0;
};

Usage QueryUsage();

struct ClearResult
{
    bool succeeded = false;
    unsigned int deletedFiles = 0;
    unsigned int lockedFiles = 0;  // 被占用、已登记为重启后删除的文件数
    DWORD lastError = ERROR_SUCCESS;
};

// 递归清空缓存目录（保留根目录本身）。被占用的文件退化为重启后删除。
ClearResult Clear();

// 把 HKCU\Software\Touch-Rev 下的 PDB/Hook 状态复位，使 UI 立即反映"下次启用将重新下载"。
void ResetStatus();

std::wstring FormatBytes(unsigned long long bytes);
}
