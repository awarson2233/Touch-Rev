#include "common/blocker_status.h"

#include <cwchar>

namespace touchrev {
namespace {

constexpr wchar_t kTouchRevStatusKey[] = L"Software\\Touch-Rev";

bool OpenStatusKey(HKEY* key) {
    if (!key) {
        return false;
    }

    *key = nullptr;
    return RegCreateKeyExW(HKEY_CURRENT_USER, kTouchRevStatusKey, 0, nullptr, 0,
                           KEY_WRITE, nullptr, key, nullptr) == ERROR_SUCCESS;
}

void SetDwordValue(HKEY key, PCWSTR name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

void SetStringValue(HKEY key, PCWSTR name, PCWSTR value) {
    PCWSTR text = value ? value : L"";
    DWORD bytes = static_cast<DWORD>((wcslen(text) + 1) * sizeof(wchar_t));
    RegSetValueExW(key, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(text), bytes);
}

}  // namespace

void ResetBlockerStatus() {
    HKEY key = nullptr;
    if (!OpenStatusKey(&key)) {
        return;
    }

    SetDwordValue(key, L"PdbStatus", static_cast<DWORD>(BlockerPdbStatus::None));
    SetDwordValue(key, L"PdbSource", static_cast<DWORD>(BlockerPdbSource::None));
    SetDwordValue(key, L"HookStatus", static_cast<DWORD>(BlockerHookStatus::None));
    SetDwordValue(key, L"PdbLastError", ERROR_SUCCESS);
    SetDwordValue(key, L"HookLastError", ERROR_SUCCESS);
    SetDwordValue(key, L"LastError", ERROR_SUCCESS);
    SetStringValue(key, L"PdbCachePath", L"");
    SetStringValue(key, L"PdbLoadedPath", L"");
    SetStringValue(key, L"PdbGuidAge", L"");
    SetStringValue(key, L"PdbLastEvent", L"");
    SetStringValue(key, L"HookLastEvent", L"");
    SetStringValue(key, L"LastEvent", L"");

    RegCloseKey(key);
}

void UpdateBlockerPdbStatus(BlockerPdbStatus status,
                            BlockerPdbSource source,
                            PCWSTR cachePath,
                            PCWSTR loadedPath,
                            PCWSTR guidAge,
                            DWORD lastError,
                            PCWSTR lastEvent) {
    HKEY key = nullptr;
    if (!OpenStatusKey(&key)) {
        return;
    }

    SetDwordValue(key, L"PdbStatus", static_cast<DWORD>(status));
    SetDwordValue(key, L"PdbSource", static_cast<DWORD>(source));
    SetDwordValue(key, L"PdbLastError", lastError);
    SetDwordValue(key, L"LastError", lastError);
    SetStringValue(key, L"PdbCachePath", cachePath);
    SetStringValue(key, L"PdbLoadedPath", loadedPath);
    SetStringValue(key, L"PdbGuidAge", guidAge);
    SetStringValue(key, L"PdbLastEvent", lastEvent);
    SetStringValue(key, L"LastEvent", lastEvent);

    RegCloseKey(key);
}

void UpdateBlockerHookStatus(BlockerHookStatus status,
                             DWORD lastError,
                             PCWSTR lastEvent) {
    HKEY key = nullptr;
    if (!OpenStatusKey(&key)) {
        return;
    }

    SetDwordValue(key, L"HookStatus", static_cast<DWORD>(status));
    SetDwordValue(key, L"HookLastError", lastError);
    SetDwordValue(key, L"LastError", lastError);
    SetStringValue(key, L"HookLastEvent", lastEvent);
    SetStringValue(key, L"LastEvent", lastEvent);

    RegCloseKey(key);
}

}  // namespace touchrev
