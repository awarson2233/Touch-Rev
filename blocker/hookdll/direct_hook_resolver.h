#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace touchrev {

enum class DirectHookResolveSource {
    None,
    Export,
    Symbol,
};

struct DirectHookSpec {
    PCWSTR moduleName;
    PCWSTR displayName;
    PCSTR exportName = nullptr;
    PCWSTR symbolName = nullptr;
    bool optional = false;
    bool requireExecutableAddress = false;
};

struct DirectHookResolveOptions {
    bool enableSymbolProvider = true;
};

struct DirectHookTarget {
    void* address = nullptr;
    HMODULE module = nullptr;
    std::uintptr_t rva = 0;
    DirectHookResolveSource source = DirectHookResolveSource::None;
    PCWSTR hint = L"";
    std::wstring modulePath;
};

PCWSTR DirectHookResolveSourceName(DirectHookResolveSource source);

bool ResolveDirectHookTarget(const DirectHookSpec& spec, DirectHookTarget* target);
bool ResolveDirectHookTarget(const DirectHookSpec& spec,
                             const DirectHookResolveOptions& options,
                             DirectHookTarget* target);

}  // namespace touchrev
