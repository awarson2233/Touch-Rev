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

struct TrimResult
{
    unsigned int removedVersions = 0;
    unsigned long long freedBytes = 0;
};

// 缓存布局为 symsrv 的 <root>\<pdb 名>\<签名+Age>\。每个 PDB 只保留最近 kMaxVersionsPerPdb 个版本，
// 总量仍超过 kMaxCacheBytes 时继续从最旧的版本删起；每个 PDB 的最新版本始终保留（正被 explorer 使用）。
constexpr unsigned int kMaxVersionsPerPdb = 2;
constexpr unsigned long long kMaxCacheBytes = 256ull * 1024 * 1024;
TrimResult Trim();

// 把 HKCU\Software\Touch-Rev 下的 PDB/Hook 状态复位，使 UI 立即反映"下次启用将重新下载"。
void ResetStatus();

std::wstring FormatBytes(unsigned long long bytes);
}
