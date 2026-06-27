#pragma once

#include "hookdll/direct_hook_resolver.h"

#include <windows.h>

#include <cstdint>

namespace touchrev {

struct DbgHelpSymbolResolveResult {
    void* address = nullptr;
    std::uintptr_t rva = 0;
    DWORD error = ERROR_SUCCESS;
};

bool TryResolveDbgHelpSymbolAddress(const DirectHookSpec& spec,
                                    HMODULE module,
                                    DbgHelpSymbolResolveResult* result);

}  // namespace touchrev
