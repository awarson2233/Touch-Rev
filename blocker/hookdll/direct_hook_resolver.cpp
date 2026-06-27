#include "hookdll/direct_hook_resolver.h"

#include "common/log.h"
#include "common/winutil.h"
#include "hookdll/dbghelp_symbol_provider.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace touchrev {
namespace {

struct LoadedModuleCandidate {
    HMODULE module = nullptr;
    std::wstring path;
    std::size_t imageSize = 0;
    USHORT machine = IMAGE_FILE_MACHINE_UNKNOWN;
};

std::wstring NarrowToWide(PCSTR value) {
    if (!value || value[0] == '\0') {
        return {};
    }

    int required = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
    if (required <= 1) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    int written = MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), required);
    if (written <= 1) {
        return {};
    }

    result.resize(static_cast<std::size_t>(written - 1));
    return result;
}

PCWSTR SafeString(PCWSTR value) {
    return value ? value : L"";
}

PCWSTR ApiName(const DirectHookSpec& spec) {
    return spec.displayName ? spec.displayName : SafeString(spec.moduleName);
}

bool HasPathSeparator(std::wstring_view value) {
    return value.find(L'\\') != std::wstring_view::npos ||
           value.find(L'/') != std::wstring_view::npos;
}

std::wstring GetModulePath(HMODULE module) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD capacity = static_cast<DWORD>(path.size());
        DWORD written = GetModuleFileNameW(module, path.data(), capacity);
        if (written == 0) {
            return {};
        }
        if (written < capacity) {
            path.resize(static_cast<std::size_t>(written));
            return path;
        }

        path.resize(path.size() * 2);
    }
}

bool GetModuleImageSize(HMODULE module, std::size_t* imageSize) {
    if (!module || !imageSize) {
        return false;
    }

    auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const unsigned char*>(module) + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    *imageSize = ntHeaders->OptionalHeader.SizeOfImage;
    return *imageSize != 0;
}

bool IsRangeWithinModule(HMODULE module, const void* address, std::size_t size) {
    std::size_t imageSize = 0;
    if (!GetModuleImageSize(module, &imageSize)) {
        return false;
    }

    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
    std::uintptr_t current = reinterpret_cast<std::uintptr_t>(address);
    if (current < base) {
        return false;
    }

    std::uintptr_t offset = current - base;
    return offset <= imageSize && size <= imageSize - offset;
}

bool TryComputeRva(HMODULE module, const void* address, std::uintptr_t* rva) {
    if (rva) {
        *rva = 0;
    }

    if (!module || !address) {
        return false;
    }

    std::size_t imageSize = 0;
    if (!GetModuleImageSize(module, &imageSize)) {
        return false;
    }

    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
    std::uintptr_t current = reinterpret_cast<std::uintptr_t>(address);
    if (current < base) {
        return false;
    }

    std::uintptr_t offset = current - base;
    if (offset >= imageSize) {
        return false;
    }

    if (rva) {
        *rva = offset;
    }
    return true;
}

bool IsAddressInExecutableSection(HMODULE module, const void* address) {
    if (!module || !address) {
        return false;
    }

    auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (!IsRangeWithinModule(module, dosHeader, sizeof(*dosHeader)) ||
        dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const unsigned char*>(module) + dosHeader->e_lfanew);
    if (!IsRangeWithinModule(module, ntHeaders, sizeof(*ntHeaders)) ||
        ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(ntHeaders);
    std::uintptr_t current = reinterpret_cast<std::uintptr_t>(address);
    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);

    for (WORD index = 0; index < ntHeaders->FileHeader.NumberOfSections;
         ++index, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }

        std::size_t sectionSize = std::max<std::size_t>(section->Misc.VirtualSize,
                                                       section->SizeOfRawData);
        if (sectionSize == 0) {
            continue;
        }

        std::uintptr_t start = base + section->VirtualAddress;
        std::uintptr_t end = start + sectionSize;
        if (current >= start && current < end) {
            return true;
        }
    }

    return false;
}

bool ModuleMatchesSpec(const DirectHookSpec& spec, const std::wstring& path) {
    if (!spec.moduleName || spec.moduleName[0] == L'\0') {
        return false;
    }

    std::wstring requested = spec.moduleName;
    if (HasPathSeparator(requested)) {
        std::wstring fullRequested = GetFullPath(requested);
        std::wstring fullPath = GetFullPath(path);
        if (fullRequested.empty() || fullPath.empty()) {
            return EqualsIgnoreCase(requested, path);
        }
        return EqualsIgnoreCase(fullRequested, fullPath);
    }

    return EqualsIgnoreCase(GetFileNameFromPath(path), requested);
}

bool AddModuleCandidate(HMODULE module,
                        const std::wstring& path,
                        std::vector<LoadedModuleCandidate>* candidates) {
    if (!module || !candidates) {
        return false;
    }

    for (const LoadedModuleCandidate& existing : *candidates) {
        if (existing.module == module) {
            return true;
        }
    }

    LoadedModuleCandidate candidate{};
    candidate.module = module;
    candidate.path = path.empty() ? GetModulePath(module) : path;
    if (!GetModuleImageSize(module, &candidate.imageSize)) {
        candidate.imageSize = 0;
    }
    if (!candidate.path.empty()) {
        GetPeMachineType(candidate.path, &candidate.machine);
    }

    candidates->push_back(std::move(candidate));
    return true;
}

std::vector<LoadedModuleCandidate> FindLoadedModules(const DirectHookSpec& spec) {
    std::vector<LoadedModuleCandidate> candidates;

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL ok = Module32FirstW(snapshot, &entry); ok;
             ok = Module32NextW(snapshot, &entry)) {
            std::wstring path = entry.szExePath;
            if (path.empty()) {
                path = entry.szModule;
            }
            if (!ModuleMatchesSpec(spec, path)) {
                continue;
            }

            AddModuleCandidate(reinterpret_cast<HMODULE>(entry.modBaseAddr), path,
                               &candidates);
        }
        CloseHandle(snapshot);
    }

    if (candidates.empty() && spec.moduleName && spec.moduleName[0] != L'\0') {
        HMODULE module = GetModuleHandleW(spec.moduleName);
        if (module) {
            AddModuleCandidate(module, GetModulePath(module), &candidates);
        }
    }

    return candidates;
}

bool ValidateResolvedTarget(const DirectHookSpec& spec,
                            const LoadedModuleCandidate& module,
                            void* address,
                            std::uintptr_t* rva) {
    if (!address) {
        return false;
    }

    std::uintptr_t currentRva = 0;
    if (!TryComputeRva(module.module, address, &currentRva)) {
        return false;
    }

    if (spec.requireExecutableAddress &&
        !IsAddressInExecutableSection(module.module, address)) {
        return false;
    }

    if (rva) {
        *rva = currentRva;
    }
    return true;
}

bool BuildResolvedTarget(const DirectHookSpec& spec,
                         const LoadedModuleCandidate& module,
                         void* address,
                         DirectHookResolveSource source,
                         PCWSTR hint,
                         DirectHookTarget* target) {
    std::uintptr_t rva = 0;
    if (!ValidateResolvedTarget(spec, module, address, &rva)) {
        return false;
    }

    if (target) {
        target->address = address;
        target->module = module.module;
        target->rva = rva;
        target->source = source;
        target->hint = hint ? hint : L"";
        target->modulePath = module.path;
    }
    return true;
}

bool TryResolveExport(const DirectHookSpec& spec,
                      const LoadedModuleCandidate& module,
                      DirectHookTarget* target) {
    if (!spec.exportName) {
        return false;
    }

    void* address = reinterpret_cast<void*>(GetProcAddress(module.module, spec.exportName));
    std::wstring exportName = NarrowToWide(spec.exportName);
    if (!address) {
        LogMessage(L"hookdll", spec.optional ? LogLevel::Info : LogLevel::Warning,
                   L"event=DIRECT_HOOK_EXPORT_MISS api=%s moduleName=%s export=%s "
                   L"module=0x%p path=%s reason=missing-export",
                   ApiName(spec), SafeString(spec.moduleName), exportName.c_str(),
                   module.module, module.path.c_str());
        return false;
    }

    if (BuildResolvedTarget(spec, module, address, DirectHookResolveSource::Export,
                            L"export", target)) {
        return true;
    }

    LogMessage(L"hookdll", LogLevel::Warning,
               L"event=DIRECT_HOOK_EXPORT_REJECTED api=%s moduleName=%s export=%s "
               L"module=0x%p path=%s reason=validation-failed executableRequired=%d",
               ApiName(spec), SafeString(spec.moduleName), exportName.c_str(),
               module.module, module.path.c_str(), spec.requireExecutableAddress ? 1 : 0);
    return false;
}

bool TryResolveSymbol(const DirectHookSpec& spec,
                      const DirectHookResolveOptions& options,
                      const LoadedModuleCandidate& module,
                      DirectHookTarget* target) {
    if (!spec.symbolName) {
        return false;
    }

    if (!options.enableSymbolProvider) {
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=DIRECT_HOOK_SYMBOL_SKIPPED api=%s moduleName=%s "
                   L"symbol=%s module=0x%p path=%s reason=provider-disabled",
                   ApiName(spec), SafeString(spec.moduleName), SafeString(spec.symbolName),
                   module.module, module.path.c_str());
        return false;
    }

    DbgHelpSymbolResolveResult symbolResult{};
    if (!TryResolveDbgHelpSymbolAddress(spec, module.module, &symbolResult)) {
        LogMessage(L"hookdll", spec.optional ? LogLevel::Info : LogLevel::Warning,
                   L"event=DIRECT_HOOK_SYMBOL_MISS api=%s moduleName=%s "
                   L"symbol=%s module=0x%p path=%s provider=dbghelp error=%lu",
                   ApiName(spec), SafeString(spec.moduleName), SafeString(spec.symbolName),
                   module.module, module.path.c_str(), symbolResult.error);
        return false;
    }

    if (BuildResolvedTarget(spec, module, symbolResult.address,
                            DirectHookResolveSource::Symbol, L"pdb", target)) {
        return true;
    }

    LogMessage(L"hookdll", LogLevel::Warning,
               L"event=DIRECT_HOOK_SYMBOL_REJECTED api=%s moduleName=%s "
               L"symbol=%s module=0x%p path=%s provider=dbghelp rva=0x%Ix "
               L"reason=validation-failed executableRequired=%d",
               ApiName(spec), SafeString(spec.moduleName), SafeString(spec.symbolName),
               module.module, module.path.c_str(), symbolResult.rva,
               spec.requireExecutableAddress ? 1 : 0);
    return false;
}

void LogModuleCandidate(const DirectHookSpec& spec,
                        const LoadedModuleCandidate& candidate) {
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=DIRECT_HOOK_MODULE_CANDIDATE api=%s moduleName=%s "
               L"module=0x%p imageSize=0x%Ix machine=%s path=%s",
               ApiName(spec), SafeString(spec.moduleName), candidate.module,
               candidate.imageSize, MachineTypeToString(candidate.machine),
               candidate.path.c_str());
}

void LogResolvedTarget(const DirectHookSpec& spec, const DirectHookTarget& target) {
    LogMessage(L"hookdll", LogLevel::Info,
               L"event=DIRECT_HOOK_RESOLVED api=%s moduleName=%s module=0x%p "
               L"source=%s rva=0x%Ix hint=%s path=%s",
               ApiName(spec), SafeString(spec.moduleName), target.module,
               DirectHookResolveSourceName(target.source), target.rva,
               target.hint ? target.hint : L"", target.modulePath.c_str());
}

}  // namespace

PCWSTR DirectHookResolveSourceName(DirectHookResolveSource source) {
    switch (source) {
    case DirectHookResolveSource::Export:
        return L"export";
    case DirectHookResolveSource::Symbol:
        return L"symbol";
    case DirectHookResolveSource::None:
    default:
        return L"none";
    }
}

bool ResolveDirectHookTarget(const DirectHookSpec& spec, DirectHookTarget* target) {
    return ResolveDirectHookTarget(spec, DirectHookResolveOptions{}, target);
}

bool ResolveDirectHookTarget(const DirectHookSpec& spec,
                             const DirectHookResolveOptions& options,
                             DirectHookTarget* target) {
    if (target) {
        *target = DirectHookTarget{};
    }

    std::vector<LoadedModuleCandidate> modules = FindLoadedModules(spec);
    if (modules.empty()) {
        LogMessage(L"hookdll", spec.optional ? LogLevel::Info : LogLevel::Warning,
                   L"event=DIRECT_HOOK_SKIPPED api=%s moduleName=%s "
                   L"reason=module-not-loaded",
                   ApiName(spec), SafeString(spec.moduleName));
        return false;
    }

    std::vector<DirectHookTarget> resolvedTargets;
    for (const LoadedModuleCandidate& module : modules) {
        LogModuleCandidate(spec, module);

        DirectHookTarget candidate{};
        if (TryResolveExport(spec, module, &candidate) ||
            TryResolveSymbol(spec, options, module, &candidate)) {
            resolvedTargets.push_back(std::move(candidate));
        }
    }

    if (resolvedTargets.size() == 1) {
        if (target) {
            *target = std::move(resolvedTargets.front());
            LogResolvedTarget(spec, *target);
        } else {
            LogResolvedTarget(spec, resolvedTargets.front());
        }
        return true;
    }

    if (resolvedTargets.size() > 1) {
        LogMessage(L"hookdll", LogLevel::Error,
                   L"event=DIRECT_HOOK_SKIPPED api=%s moduleName=%s "
                   L"reason=ambiguous-target matchCount=%zu",
                   ApiName(spec), SafeString(spec.moduleName), resolvedTargets.size());
        return false;
    }

    LogMessage(L"hookdll", spec.optional ? LogLevel::Info : LogLevel::Warning,
               L"event=DIRECT_HOOK_SKIPPED api=%s moduleName=%s "
               L"reason=no-target moduleCount=%zu",
               ApiName(spec), SafeString(spec.moduleName), modules.size());
    return false;
}

}  // namespace touchrev
