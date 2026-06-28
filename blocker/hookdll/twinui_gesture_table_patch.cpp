#include "hookdll/twinui_gesture_table_patch.h"

#include "common/log.h"
#include "common/winutil.h"
#include "hookdll/direct_hook_resolver.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace touchrev {
namespace {

constexpr wchar_t kTwinuiPcshellModuleName[] = L"twinui.pcshell.dll";
constexpr wchar_t kGestureTargetsSymbol[] =
    L"TouchGestureSettings::s_gestureTargets";
constexpr wchar_t kGestureTargetsForGamingSymbol[] =
    L"TouchGestureSettings::s_gestureTargetsForGamingFullScreenExperience";
constexpr wchar_t kGestureTargetsEndSymbol[] =
    L"HotkeyHandler::MoreSnapZonesHotkeyMaps::Default";
constexpr std::size_t kMaxGestureTableEntries = 32;

struct TouchGestureTargetEntry {
    int type;
    int fingerCount;
    GUID target;
};

static_assert(sizeof(TouchGestureTargetEntry) == 24);
static_assert(offsetof(TouchGestureTargetEntry, target) == 8);

struct BlockedGestureTarget {
    int type;
    int fingerCount;
    PCWSTR reason;
};

constexpr std::array<BlockedGestureTarget, 3> kBlockedGestureTargets{{
    {1, 3, L"three-finger-pointer-down"},
    {2, 3, L"three-finger-horizontal-swipe"},
    {4, 3, L"three-finger-vertical-swipe"},
}};

struct GestureTableRange {
    PCWSTR displayName = L"";
    TouchGestureTargetEntry* entries = nullptr;
    std::size_t count = 0;
};

struct PatchRecord {
    PCWSTR tableName = L"";
    std::size_t index = 0;
    TouchGestureTargetEntry* entry = nullptr;
    TouchGestureTargetEntry original{};
};

std::mutex g_tablePatchMutex;
bool g_tablePatchInstalled = false;
std::vector<PatchRecord> g_patchRecords;

std::wstring GuidToString(const GUID& guid) {
    wchar_t buffer[64]{};
    swprintf_s(buffer,
               L"%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
               guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
               guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
               guid.Data4[6], guid.Data4[7]);
    return buffer;
}

PCWSTR FindBlockedGestureReason(int type, int fingerCount) {
    for (const BlockedGestureTarget& target : kBlockedGestureTargets) {
        if (target.type == type && target.fingerCount == fingerCount) {
            return target.reason;
        }
    }
    return nullptr;
}

bool ResolveDataSymbol(PCWSTR displayName,
                       PCWSTR symbolName,
                       DirectHookTarget* target) {
    DirectHookSpec spec{};
    spec.moduleName = kTwinuiPcshellModuleName;
    spec.displayName = displayName;
    spec.symbolName = symbolName;
    spec.optional = true;
    spec.requireExecutableAddress = false;

    return ResolveDirectHookTarget(spec, target);
}

bool BuildGestureTableRange(const DirectHookTarget& begin,
                            const DirectHookTarget& end,
                            PCWSTR displayName,
                            GestureTableRange* range) {
    if (range) {
        *range = GestureTableRange{};
    }
    if (!range || !begin.address || !end.address || begin.module != end.module) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=TWINUI_GESTURE_TABLE_RANGE_FAILED table=%s reason=invalid-symbols begin=0x%p end=0x%p beginModule=0x%p endModule=0x%p",
                   displayName ? displayName : L"", begin.address, end.address,
                   begin.module, end.module);
        return false;
    }

    std::uintptr_t beginAddress = reinterpret_cast<std::uintptr_t>(begin.address);
    std::uintptr_t endAddress = reinterpret_cast<std::uintptr_t>(end.address);
    if (endAddress <= beginAddress) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=TWINUI_GESTURE_TABLE_RANGE_FAILED table=%s reason=non-increasing begin=0x%p end=0x%p",
                   displayName ? displayName : L"", begin.address, end.address);
        return false;
    }

    std::uintptr_t byteCount = endAddress - beginAddress;
    if ((byteCount % sizeof(TouchGestureTargetEntry)) != 0) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=TWINUI_GESTURE_TABLE_RANGE_FAILED table=%s reason=unaligned byteCount=0x%Ix entrySize=0x%Ix",
                   displayName ? displayName : L"", byteCount,
                   sizeof(TouchGestureTargetEntry));
        return false;
    }

    std::size_t entryCount =
        static_cast<std::size_t>(byteCount / sizeof(TouchGestureTargetEntry));
    if (entryCount == 0 || entryCount > kMaxGestureTableEntries) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=TWINUI_GESTURE_TABLE_RANGE_FAILED table=%s reason=count-out-of-range count=%zu max=%zu",
                   displayName ? displayName : L"", entryCount,
                   kMaxGestureTableEntries);
        return false;
    }

    range->displayName = displayName ? displayName : L"";
    range->entries = reinterpret_cast<TouchGestureTargetEntry*>(begin.address);
    range->count = entryCount;
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=TWINUI_GESTURE_TABLE_RANGE table=%s begin=0x%p end=0x%p count=%zu",
               range->displayName, begin.address, end.address, range->count);
    return true;
}

void LogGestureTableEntries(const GestureTableRange& range) {
    if (!range.entries) {
        return;
    }

    for (std::size_t index = 0; index < range.count; ++index) {
        const TouchGestureTargetEntry& entry = range.entries[index];
        PCWSTR reason = FindBlockedGestureReason(entry.type, entry.fingerCount);
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=TWINUI_GESTURE_TABLE_ENTRY table=%s index=%zu entry=0x%p type=%d fingerCount=%d target=%s blocked=%d reason=%s",
                   range.displayName, index, range.entries + index, entry.type,
                   entry.fingerCount, GuidToString(entry.target).c_str(),
                   reason ? 1 : 0, reason ? reason : L"");
    }
}

bool WriteGestureTableEntry(TouchGestureTargetEntry* entry,
                            const TouchGestureTargetEntry& value,
                            PCWSTR operation,
                            PCWSTR tableName,
                            std::size_t index) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &oldProtect)) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_GESTURE_TABLE_WRITE_FAILED op=%s table=%s index=%zu entry=0x%p error=%lu message=%s",
                   operation ? operation : L"", tableName ? tableName : L"", index,
                   entry, error, FormatLastError(error).c_str());
        return false;
    }

    *entry = value;

    DWORD ignoredProtect = 0;
    if (!VirtualProtect(entry, sizeof(*entry), oldProtect, &ignoredProtect)) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_GESTURE_TABLE_PROTECT_RESTORE_FAILED op=%s table=%s index=%zu entry=0x%p error=%lu message=%s",
                   operation ? operation : L"", tableName ? tableName : L"", index,
                   entry, error, FormatLastError(error).c_str());
        return false;
    }

    return true;
}

bool PatchGestureTableEntry(const GestureTableRange& range,
                            std::size_t index,
                            PCWSTR reason,
                            std::vector<PatchRecord>* records) {
    if (!records || !range.entries || index >= range.count) {
        return false;
    }

    TouchGestureTargetEntry* entry = range.entries + index;
    TouchGestureTargetEntry original = *entry;
    TouchGestureTargetEntry patched = original;
    patched.type = 0;
    patched.fingerCount = 0;

    records->push_back(PatchRecord{range.displayName, index, entry, original});
    if (!WriteGestureTableEntry(entry, patched, L"patch", range.displayName, index)) {
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=TWINUI_GESTURE_TABLE_PATCHED table=%s index=%zu entry=0x%p oldType=%d oldFingerCount=%d newType=%d newFingerCount=%d target=%s reason=%s",
               range.displayName, index, entry, original.type, original.fingerCount,
               patched.type, patched.fingerCount,
               GuidToString(original.target).c_str(), reason ? reason : L"");
    return true;
}

bool PatchGestureTable(const GestureTableRange& range,
                       std::vector<PatchRecord>* records) {
    for (std::size_t index = 0; index < range.count; ++index) {
        const TouchGestureTargetEntry& entry = range.entries[index];
        PCWSTR reason = FindBlockedGestureReason(entry.type, entry.fingerCount);
        if (!reason) {
            continue;
        }

        if (!PatchGestureTableEntry(range, index, reason, records)) {
            return false;
        }
    }

    return true;
}

bool RestorePatchRecord(const PatchRecord& record) {
    if (!WriteGestureTableEntry(record.entry, record.original, L"restore",
                                record.tableName, record.index)) {
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=TWINUI_GESTURE_TABLE_RESTORED table=%s index=%zu entry=0x%p type=%d fingerCount=%d target=%s",
               record.tableName, record.index, record.entry, record.original.type,
               record.original.fingerCount,
               GuidToString(record.original.target).c_str());
    return true;
}

void RestoreAppliedPatches(std::vector<PatchRecord>* records) {
    if (!records) {
        return;
    }

    for (auto it = records->rbegin(); it != records->rend(); ++it) {
        RestorePatchRecord(*it);
    }
    records->clear();
}

}  // namespace

bool InstallTwinuiGestureTablePatch() {
    std::lock_guard<std::mutex> lock(g_tablePatchMutex);
    if (g_tablePatchInstalled) {
        return true;
    }

    UpdateRegistryStatus(1, 0);

    DirectHookTarget normalTableStart{};
    DirectHookTarget gamingTableStart{};
    if (!ResolveDataSymbol(L"TouchGestureSettings::s_gestureTargets",
                           kGestureTargetsSymbol, &normalTableStart) ||
        !ResolveDataSymbol(
            L"TouchGestureSettings::s_gestureTargetsForGamingFullScreenExperience",
            kGestureTargetsForGamingSymbol, &gamingTableStart)) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=TWINUI_GESTURE_TABLE_PATCH_SKIPPED reason=primary-symbol-resolve-failed");
        UpdateRegistryStatus(3, 2);
        return false;
    }

    GestureTableRange normalRange{};
    if (!BuildGestureTableRange(normalTableStart, gamingTableStart,
                                L"s_gestureTargets", &normalRange)) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=TWINUI_GESTURE_TABLE_PATCH_SKIPPED reason=normal-range-validation-failed");
        UpdateRegistryStatus(3, 2);
        return false;
    }

    DirectHookTarget gamingTableEnd{};
    GestureTableRange gamingRange{};
    bool hasGamingRange = false;
    if (ResolveDataSymbol(L"HotkeyHandler::MoreSnapZonesHotkeyMaps::Default",
                          kGestureTargetsEndSymbol, &gamingTableEnd)) {
        hasGamingRange = BuildGestureTableRange(
            gamingTableStart, gamingTableEnd,
            L"s_gestureTargetsForGamingFullScreenExperience", &gamingRange);
    }
    if (!hasGamingRange) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=TWINUI_GESTURE_TABLE_GAMING_PATCH_SKIPPED reason=end-symbol-or-range-validation-failed");
    }

    LogGestureTableEntries(normalRange);
    if (hasGamingRange) {
        LogGestureTableEntries(gamingRange);
    }

    std::vector<PatchRecord> records;
    records.reserve(normalRange.count + (hasGamingRange ? gamingRange.count : 0));
    if (!PatchGestureTable(normalRange, &records) ||
        (hasGamingRange && !PatchGestureTable(gamingRange, &records))) {
        RestoreAppliedPatches(&records);
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_GESTURE_TABLE_PATCH_FAILED reason=write-failed");
        UpdateRegistryStatus(3, 2);
        return false;
    }

    if (records.empty()) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=TWINUI_GESTURE_TABLE_PATCH_SKIPPED reason=no-matching-entries normalCount=%zu gamingCount=%zu",
                   normalRange.count, gamingRange.count);
        UpdateRegistryStatus(3, 2);
        return false;
    }

    g_patchRecords = std::move(records);
    g_tablePatchInstalled = true;
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=TWINUI_GESTURE_TABLE_PATCH_INSTALLED patchCount=%zu",
               g_patchRecords.size());
    
    UpdateRegistryStatus(2, 1);
    return true;
}

bool UninstallTwinuiGestureTablePatch() {
    std::lock_guard<std::mutex> lock(g_tablePatchMutex);
    if (!g_tablePatchInstalled) {
        return true;
    }

    bool ok = true;
    for (auto it = g_patchRecords.rbegin(); it != g_patchRecords.rend(); ++it) {
        ok = RestorePatchRecord(*it) && ok;
    }

    if (!ok) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=TWINUI_GESTURE_TABLE_RESTORE_FAILED patchCount=%zu",
                   g_patchRecords.size());
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=TWINUI_GESTURE_TABLE_PATCH_UNINSTALLED patchCount=%zu",
               g_patchRecords.size());
    g_patchRecords.clear();
    g_tablePatchInstalled = false;

    UpdateRegistryStatus(2, 0);
    return true;
}

}  // namespace touchrev
