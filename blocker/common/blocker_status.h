#pragma once

#include <windows.h>

namespace touchrev {

enum class BlockerPdbStatus : DWORD {
    None = 0,
    Resolving = 1,
    Loaded = 2,
    DownloadFailed = 3,
    IdentityMismatch = 4,
};

enum class BlockerPdbSource : DWORD {
    None = 0,
    Cache = 1,
    Download = 2,
    External = 3,
};

enum class BlockerHookStatus : DWORD {
    None = 0,
    Installed = 1,
    SymbolResolveFailed = 2,
    RangeValidationFailed = 3,
    NoMatchingEntries = 4,
    WriteFailed = 5,
};

void ResetBlockerStatus();

void UpdateBlockerPdbStatus(BlockerPdbStatus status,
                            BlockerPdbSource source,
                            PCWSTR cachePath,
                            PCWSTR loadedPath,
                            PCWSTR guidAge,
                            DWORD lastError,
                            PCWSTR lastEvent);

void UpdateBlockerHookStatus(BlockerHookStatus status,
                             DWORD lastError,
                             PCWSTR lastEvent);

}  // namespace touchrev
