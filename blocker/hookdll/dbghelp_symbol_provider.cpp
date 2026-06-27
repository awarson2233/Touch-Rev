#include "hookdll/dbghelp_symbol_provider.h"

#include "common/log.h"
#include "common/winutil.h"

#include <dbghelp.h>
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#ifndef SSRVOPT_GUIDPTR
#define SSRVOPT_GUIDPTR 0x00000008
#endif

namespace touchrev {
namespace {

constexpr DWORD kCodeViewRsdsSignature = 0x53445352;  // "RSDS"
constexpr PCWSTR kMicrosoftSymbolServer = L"https://msdl.microsoft.com/download/symbols";

struct PdbIdentity {
    GUID guid{};
    DWORD age = 0;
    std::wstring pdbName;
    bool valid = false;
};

struct SymbolMatchContext {
    const DirectHookSpec* spec = nullptr;
    HMODULE module = nullptr;
    DWORD64 matchAddress = 0;
    std::uintptr_t matchRva = 0;
    std::wstring matchName;
    DWORD matchCount = 0;
};

struct CvInfoPdb70 {
    DWORD signature;
    GUID guid;
    DWORD age;
    char pdbFileName[1];
};

std::mutex g_dbgHelpMutex;
HANDLE g_symbolSession = nullptr;
bool g_symbolSessionInitialized = false;

PCWSTR SafeString(PCWSTR value) {
    return value ? value : L"";
}

PCWSTR ApiName(const DirectHookSpec& spec) {
    return spec.displayName ? spec.displayName : SafeString(spec.moduleName);
}

std::wstring NarrowToWide(PCSTR value, std::size_t maxChars) {
    if (!value || maxChars == 0) {
        return {};
    }

    std::size_t length = 0;
    while (length < maxChars && value[length] != '\0') {
        ++length;
    }
    if (length == 0) {
        return {};
    }

    int required = MultiByteToWideChar(CP_ACP, 0, value,
                                       static_cast<int>(length), nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    int written = MultiByteToWideChar(CP_ACP, 0, value,
                                      static_cast<int>(length), result.data(),
                                      required);
    if (written <= 0) {
        return {};
    }

    result.resize(static_cast<std::size_t>(written));
    return result;
}

std::wstring GetEnvironmentString(PCWSTR name) {
    DWORD chars = GetEnvironmentVariableW(name, nullptr, 0);
    if (chars == 0) {
        return {};
    }

    std::wstring value(static_cast<std::size_t>(chars), L'\0');
    DWORD written = GetEnvironmentVariableW(name, value.data(), chars);
    if (written == 0 || written >= chars) {
        return {};
    }

    value.resize(static_cast<std::size_t>(written));
    return value;
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

std::wstring GetDirectoryName(const std::wstring& path) {
    std::size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return {};
    }
    return path.substr(0, pos);
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }

    if (CreateDirectoryW(path.c_str(), nullptr)) {
        return true;
    }

    DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS;
}

std::wstring BuildSymbolCacheRoot() {
    std::wstring localAppData = GetEnvironmentString(L"LOCALAPPDATA");
    if (localAppData.empty()) {
        return {};
    }

    std::wstring root = localAppData + L"\\Touch-Rev";
    std::wstring symbol = root + L"\\symbol";
    if (!EnsureDirectory(root) || !EnsureDirectory(symbol)) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_CACHE_DIR_FAILED root=%s symbol=%s error=%lu message=%s",
                   root.c_str(), symbol.c_str(), error, FormatLastError(error).c_str());
        return {};
    }

    return symbol;
}

std::wstring BuildNetworkSymbolPath(const std::wstring& cacheRoot) {
    return L"srv*" + cacheRoot + L"*" + kMicrosoftSymbolServer;
}

bool GetModuleImageSize(HMODULE module, DWORD* imageSize) {
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
    DWORD imageSize = 0;
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

bool TryComputeRva(HMODULE module, DWORD64 address, std::uintptr_t* rva) {
    if (rva) {
        *rva = 0;
    }

    DWORD imageSize = 0;
    if (!GetModuleImageSize(module, &imageSize)) {
        return false;
    }

    DWORD64 base = reinterpret_cast<DWORD64>(module);
    if (address < base) {
        return false;
    }

    DWORD64 offset = address - base;
    if (offset >= imageSize) {
        return false;
    }

    if (rva) {
        *rva = static_cast<std::uintptr_t>(offset);
    }
    return true;
}

std::wstring GuidToString(const GUID& guid) {
    wchar_t buffer[64]{};
    swprintf_s(buffer,
               L"%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
               guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
               guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
               guid.Data4[6], guid.Data4[7]);
    return buffer;
}

bool SameGuid(const GUID& left, const GUID& right) {
    return std::memcmp(&left, &right, sizeof(GUID)) == 0;
}

std::wstring ModuleStem(PCWSTR moduleName) {
    std::wstring stem = SafeString(moduleName);
    std::size_t slash = stem.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        stem = stem.substr(slash + 1);
    }

    constexpr std::wstring_view dllSuffix = L".dll";
    if (stem.size() > dllSuffix.size() &&
        EqualsIgnoreCase(std::wstring_view(stem).substr(stem.size() - dllSuffix.size()),
                         dllSuffix)) {
        stem.resize(stem.size() - dllSuffix.size());
    }

    return stem;
}

std::vector<std::wstring> BuildSymbolQueries(const DirectHookSpec& spec) {
    std::vector<std::wstring> queries;
    if (!spec.symbolName || spec.symbolName[0] == L'\0') {
        return queries;
    }

    std::wstring symbolName = spec.symbolName;
    queries.push_back(symbolName);

    std::wstring moduleStem = ModuleStem(spec.moduleName);
    if (!moduleStem.empty()) {
        queries.push_back(moduleStem + L"!" + symbolName);
    }

    if (spec.moduleName && spec.moduleName[0] != L'\0') {
        queries.push_back(std::wstring(spec.moduleName) + L"!" + symbolName);
    }

    return queries;
}

std::vector<std::wstring> BuildSymbolTokens(PCWSTR symbolName) {
    std::vector<std::wstring> tokens;
    if (!symbolName || symbolName[0] == L'\0') {
        return tokens;
    }

    std::wstring value = symbolName;
    std::size_t start = 0;
    while (start < value.size()) {
        std::size_t pos = value.find(L"::", start);
        std::wstring token = value.substr(start, pos == std::wstring::npos
                                                     ? std::wstring::npos
                                                     : pos - start);
        if (!token.empty()) {
            tokens.push_back(token);
        }
        if (pos == std::wstring::npos) {
            break;
        }
        start = pos + 2;
    }

    return tokens;
}

bool SymbolNameMatches(PCWSTR requestedName, std::wstring_view actualName) {
    if (!requestedName || requestedName[0] == L'\0' || actualName.empty()) {
        return false;
    }

    std::wstring requested = requestedName;
    if (EqualsIgnoreCase(actualName, requested)) {
        return true;
    }

    std::wstring actual(actualName);
    if (actual.find(requested) != std::wstring::npos) {
        return true;
    }

    std::vector<std::wstring> tokens = BuildSymbolTokens(requestedName);
    return !tokens.empty() &&
           std::all_of(tokens.begin(), tokens.end(), [&actual](const std::wstring& token) {
               return actual.find(token) != std::wstring::npos;
           });
}

std::wstring BuildSymbolEnumMask(PCWSTR symbolName) {
    std::vector<std::wstring> tokens = BuildSymbolTokens(symbolName);
    if (!tokens.empty()) {
        return L"*" + tokens.back() + L"*";
    }
    return L"*" + std::wstring(SafeString(symbolName)) + L"*";
}

bool TryReadModulePdbIdentity(HMODULE module, PdbIdentity* identity) {
    if (identity) {
        *identity = PdbIdentity{};
    }
    if (!module || !identity) {
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

    const IMAGE_DATA_DIRECTORY& debugDirectory =
        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (debugDirectory.VirtualAddress == 0 || debugDirectory.Size == 0) {
        return false;
    }

    auto* debugEntries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(
        reinterpret_cast<const unsigned char*>(module) + debugDirectory.VirtualAddress);
    if (!IsRangeWithinModule(module, debugEntries, debugDirectory.Size)) {
        return false;
    }

    DWORD entryCount = debugDirectory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    for (DWORD index = 0; index < entryCount; ++index) {
        const IMAGE_DEBUG_DIRECTORY& entry = debugEntries[index];
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
            entry.SizeOfData < offsetof(CvInfoPdb70, pdbFileName) ||
            entry.AddressOfRawData == 0) {
            continue;
        }

        auto* cv = reinterpret_cast<const CvInfoPdb70*>(
            reinterpret_cast<const unsigned char*>(module) + entry.AddressOfRawData);
        if (!IsRangeWithinModule(module, cv, entry.SizeOfData) ||
            cv->signature != kCodeViewRsdsSignature) {
            continue;
        }

        std::size_t nameCapacity =
            entry.SizeOfData - offsetof(CvInfoPdb70, pdbFileName);
        identity->guid = cv->guid;
        identity->age = cv->age;
        identity->pdbName = NarrowToWide(cv->pdbFileName, nameCapacity);
        identity->valid = true;
        return true;
    }

    return false;
}

bool PdbInfoMatchesIdentity(const IMAGEHLP_MODULEW64& moduleInfo,
                            const PdbIdentity& identity) {
    if (!identity.valid) {
        return false;
    }

    return moduleInfo.SymType == SymPdb && !moduleInfo.PdbUnmatched &&
           !moduleInfo.DbgUnmatched && moduleInfo.PdbAge == identity.age &&
           SameGuid(moduleInfo.PdbSig70, identity.guid);
}

bool InitializeDbgHelpSession() {
    if (g_symbolSessionInitialized) {
        return true;
    }

    if (!g_symbolSession) {
        g_symbolSession = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_symbolSession) {
            DWORD error = GetLastError();
            LogMessage(L"hookdll", LogLevel::Warning,
                       L"event=DBGHELP_SESSION_FAILED error=%lu message=%s",
                       error, FormatLastError(error).c_str());
            return false;
        }
    }

    DWORD options = SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS |
                    SYMOPT_NO_PROMPTS | SYMOPT_EXACT_SYMBOLS;
    SymSetOptions(options);

    if (!SymInitializeW(g_symbolSession, nullptr, FALSE)) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=DBGHELP_INIT_FAILED error=%lu message=%s",
                   error, FormatLastError(error).c_str());
        return false;
    }

    g_symbolSessionInitialized = true;
    LogMessage(L"hookdll", LogLevel::Info, L"event=DBGHELP_INIT_OK");
    return true;
}

bool SetSymbolSearchPath(const std::wstring& symbolPath, PCWSTR mode) {
    if (!SymSetSearchPathW(g_symbolSession, symbolPath.c_str())) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_SYMBOL_PATH_FAILED mode=%s error=%lu message=%s path=%s",
                   SafeString(mode), error, FormatLastError(error).c_str(),
                   symbolPath.c_str());
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=PDB_SYMBOL_PATH_SET mode=%s path=%s", SafeString(mode),
               symbolPath.c_str());
    return true;
}

void UnloadModuleSymbols(DWORD64 base) {
    if (base != 0) {
        SymUnloadModule64(g_symbolSession, base);
    }
}

bool QueryLoadedPdbInfo(DWORD64 base, IMAGEHLP_MODULEW64* moduleInfo) {
    if (!moduleInfo) {
        return false;
    }

    *moduleInfo = IMAGEHLP_MODULEW64{};
    moduleInfo->SizeOfStruct = sizeof(IMAGEHLP_MODULEW64);
    return SymGetModuleInfoW64(g_symbolSession, base, moduleInfo) != FALSE;
}

std::wstring PdbFileNameFromIdentity(const PdbIdentity& identity) {
    std::wstring pdbFileName = GetFileNameFromPath(identity.pdbName);
    return pdbFileName.empty() ? identity.pdbName : pdbFileName;
}

std::wstring GuidToSymbolIndex(const GUID& guid, DWORD age) {
    wchar_t buffer[80]{};
    swprintf_s(buffer,
               L"%08lX%04hX%04hX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%lX",
               guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
               guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
               guid.Data4[6], guid.Data4[7], age);
    return buffer;
}

bool EnsureDirectoryTree(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }

    std::wstring normalized = path;
    for (wchar_t& ch : normalized) {
        if (ch == L'/') {
            ch = L'\\';
        }
    }

    std::size_t start = 0;
    if (normalized.size() >= 2 && normalized[1] == L':') {
        start = 3;
    } else if (normalized.rfind(L"\\\\", 0) == 0) {
        std::size_t first = normalized.find(L'\\', 2);
        std::size_t second = first == std::wstring::npos
                                 ? std::wstring::npos
                                 : normalized.find(L'\\', first + 1);
        start = second == std::wstring::npos ? normalized.size() : second + 1;
    }

    for (std::size_t pos = normalized.find(L'\\', start); pos != std::wstring::npos;
         pos = normalized.find(L'\\', pos + 1)) {
        if (!EnsureDirectory(normalized.substr(0, pos))) {
            return false;
        }
    }

    return EnsureDirectory(normalized);
}

std::wstring BuildExactPdbCachePath(const std::wstring& cacheRoot,
                                    const PdbIdentity& identity) {
    std::wstring pdbFileName = PdbFileNameFromIdentity(identity);
    if (cacheRoot.empty() || pdbFileName.empty()) {
        return {};
    }

    return cacheRoot + L"\\" + pdbFileName + L"\\" +
           GuidToSymbolIndex(identity.guid, identity.age) + L"\\" + pdbFileName;
}

std::wstring BuildPdbDownloadPath(const PdbIdentity& identity) {
    std::wstring pdbFileName = PdbFileNameFromIdentity(identity);
    if (pdbFileName.empty()) {
        return {};
    }

    return L"/download/symbols/" + pdbFileName + L"/" +
           GuidToSymbolIndex(identity.guid, identity.age) + L"/" + pdbFileName;
}

bool FileExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DownloadPdbFromMicrosoft(const DirectHookSpec& spec,
                              HMODULE module,
                              const PdbIdentity& identity,
                              const std::wstring& targetPath,
                              DWORD* errorOut) {
    if (errorOut) {
        *errorOut = ERROR_SUCCESS;
    }

    std::wstring pdbFileName = PdbFileNameFromIdentity(identity);
    std::wstring downloadPath = BuildPdbDownloadPath(identity);
    if (pdbFileName.empty() || downloadPath.empty()) {
        if (errorOut) {
            *errorOut = ERROR_INVALID_DATA;
        }
        return false;
    }

    std::wstring targetDirectory = GetDirectoryName(targetPath);
    if (!EnsureDirectoryTree(targetDirectory)) {
        DWORD error = GetLastError();
        if (errorOut) {
            *errorOut = error;
        }
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_CACHE_DIR_FAILED path=%s error=%lu message=%s",
                   targetDirectory.c_str(), error, FormatLastError(error).c_str());
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=PDB_DOWNLOAD_START api=%s moduleName=%s module=0x%p pdb=%s guid=%s age=%lu url=https://msdl.microsoft.com%s target=%s",
               ApiName(spec), SafeString(spec.moduleName), module, pdbFileName.c_str(),
               GuidToString(identity.guid).c_str(), identity.age, downloadPath.c_str(),
               targetPath.c_str());

    HANDLE session = WinHttpOpen(L"TouchRevHook/0.1",
                                 WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        DWORD error = GetLastError();
        if (errorOut) {
            *errorOut = error;
        }
        return false;
    }

    HANDLE connect = WinHttpConnect(session, L"msdl.microsoft.com",
                                    INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        DWORD error = GetLastError();
        WinHttpCloseHandle(session);
        if (errorOut) {
            *errorOut = error;
        }
        return false;
    }

    HANDLE request = WinHttpOpenRequest(connect, L"GET", downloadPath.c_str(),
                                        nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE);
    if (!request) {
        DWORD error = GetLastError();
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        if (errorOut) {
            *errorOut = error;
        }
        return false;
    }

    bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (ok && (!WinHttpQueryHeaders(request,
                                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode,
                                    &statusCodeSize, WINHTTP_NO_HEADER_INDEX) ||
               statusCode != HTTP_STATUS_OK)) {
        ok = false;
        error = statusCode == 0 ? GetLastError() : ERROR_FILE_NOT_FOUND;
    }

    std::wstring tempPath = targetPath + L".tmp";
    HANDLE file = INVALID_HANDLE_VALUE;
    if (ok) {
        file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            ok = false;
            error = GetLastError();
        }
    }

    DWORD64 totalBytes = 0;
    while (ok) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            ok = false;
            error = GetLastError();
            break;
        }
        if (available == 0) {
            break;
        }

        std::vector<unsigned char> buffer(available);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &bytesRead)) {
            ok = false;
            error = GetLastError();
            break;
        }
        if (bytesRead == 0) {
            break;
        }

        DWORD bytesWritten = 0;
        if (!WriteFile(file, buffer.data(), bytesRead, &bytesWritten, nullptr) ||
            bytesWritten != bytesRead) {
            ok = false;
            error = GetLastError();
            break;
        }
        totalBytes += bytesRead;
    }

    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (!ok || totalBytes == 0) {
        DeleteFileW(tempPath.c_str());
        if (errorOut) {
            *errorOut = error == ERROR_SUCCESS ? ERROR_FILE_NOT_FOUND : error;
        }
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_DOWNLOAD_FAILED api=%s moduleName=%s module=0x%p pdb=%s guid=%s age=%lu status=%lu error=%lu message=%s",
                   ApiName(spec), SafeString(spec.moduleName), module,
                   pdbFileName.c_str(), GuidToString(identity.guid).c_str(),
                   identity.age, statusCode,
                   error == ERROR_SUCCESS ? ERROR_FILE_NOT_FOUND : error,
                   FormatLastError(error == ERROR_SUCCESS ? ERROR_FILE_NOT_FOUND : error)
                       .c_str());
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), targetPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        error = GetLastError();
        DeleteFileW(tempPath.c_str());
        if (errorOut) {
            *errorOut = error;
        }
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_DOWNLOAD_MOVE_FAILED api=%s moduleName=%s module=0x%p target=%s error=%lu message=%s",
                   ApiName(spec), SafeString(spec.moduleName), module, targetPath.c_str(),
                   error, FormatLastError(error).c_str());
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=PDB_DOWNLOAD_OK api=%s moduleName=%s module=0x%p pdb=%s guid=%s age=%lu bytes=%llu found=%s",
               ApiName(spec), SafeString(spec.moduleName), module, pdbFileName.c_str(),
               GuidToString(identity.guid).c_str(), identity.age, totalBytes,
               targetPath.c_str());
    return true;
}

bool LoadModuleSymbolsFromExactPdb(const DirectHookSpec& spec,
                                   HMODULE module,
                                   const std::wstring& foundPdbPath,
                                   DbgHelpSymbolResolveResult* result) {
    std::wstring modulePath = GetModulePath(module);
    if (modulePath.empty()) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_MODULE_PATH_FAILED api=%s moduleName=%s "
                   L"module=0x%p error=%lu message=%s",
                   ApiName(spec), SafeString(spec.moduleName), module, error,
                   FormatLastError(error).c_str());
        if (result) {
            result->error = error;
        }
        return false;
    }

    DWORD imageSize = 0;
    if (!GetModuleImageSize(module, &imageSize)) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_MODULE_SIZE_FAILED api=%s moduleName=%s module=0x%p",
                   ApiName(spec), SafeString(spec.moduleName), module);
        if (result) {
            result->error = ERROR_INVALID_DATA;
        }
        return false;
    }

    std::wstring pdbDirectory = GetDirectoryName(foundPdbPath);
    if (pdbDirectory.empty()) {
        if (result) {
            result->error = ERROR_PATH_NOT_FOUND;
        }
        return false;
    }

    DWORD64 base = reinterpret_cast<DWORD64>(module);
    UnloadModuleSymbols(base);
    if (!SetSymbolSearchPath(pdbDirectory, L"exact-pdb")) {
        if (result) {
            result->error = GetLastError();
        }
        return false;
    }

    DWORD64 loadedBase = SymLoadModuleExW(g_symbolSession, nullptr, modulePath.c_str(),
                                          spec.moduleName, base, imageSize, nullptr, 0);
    if (loadedBase == 0) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_MODULE_LOAD_FAILED api=%s moduleName=%s module=0x%p "
                   L"image=%s pdb=%s error=%lu message=%s",
                   ApiName(spec), SafeString(spec.moduleName), module,
                   modulePath.c_str(), foundPdbPath.c_str(), error,
                   FormatLastError(error).c_str());
        if (result) {
            result->error = error;
        }
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=PDB_MODULE_LOAD_OK api=%s moduleName=%s module=0x%p image=%s pdb=%s",
               ApiName(spec), SafeString(spec.moduleName), module, modulePath.c_str(),
               foundPdbPath.c_str());
    return true;
}

bool EnsureExactSymbolsLoaded(const DirectHookSpec& spec,
                              HMODULE module,
                              PdbIdentity* identity,
                              DbgHelpSymbolResolveResult* result) {
    if (identity) {
        *identity = PdbIdentity{};
    }

    PdbIdentity currentIdentity{};
    if (!TryReadModulePdbIdentity(module, &currentIdentity)) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_IDENTITY_FAILED api=%s moduleName=%s module=0x%p",
                   ApiName(spec), SafeString(spec.moduleName), module);
        if (result) {
            result->error = ERROR_INVALID_DATA;
        }
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=PDB_IDENTITY_DLL api=%s moduleName=%s module=0x%p pdb=%s guid=%s age=%lu",
               ApiName(spec), SafeString(spec.moduleName), module,
               currentIdentity.pdbName.c_str(), GuidToString(currentIdentity.guid).c_str(),
               currentIdentity.age);

    std::wstring cacheRoot = BuildSymbolCacheRoot();
    if (cacheRoot.empty()) {
        if (result) {
            result->error = ERROR_PATH_NOT_FOUND;
        }
        return false;
    }

    std::wstring foundPdbPath = BuildExactPdbCachePath(cacheRoot, currentIdentity);
    if (foundPdbPath.empty()) {
        if (result) {
            result->error = ERROR_INVALID_DATA;
        }
        return false;
    }

    if (FileExists(foundPdbPath)) {
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=PDB_CACHE_MATCH api=%s moduleName=%s module=0x%p pdb=%s guid=%s age=%lu found=%s",
                   ApiName(spec), SafeString(spec.moduleName), module,
                   PdbFileNameFromIdentity(currentIdentity).c_str(),
                   GuidToString(currentIdentity.guid).c_str(), currentIdentity.age,
                   foundPdbPath.c_str());
    } else {
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=PDB_CACHE_MISS api=%s moduleName=%s module=0x%p pdb=%s guid=%s age=%lu path=%s",
                   ApiName(spec), SafeString(spec.moduleName), module,
                   PdbFileNameFromIdentity(currentIdentity).c_str(),
                   GuidToString(currentIdentity.guid).c_str(), currentIdentity.age,
                   foundPdbPath.c_str());

        DWORD error = ERROR_SUCCESS;
        if (!DownloadPdbFromMicrosoft(spec, module, currentIdentity, foundPdbPath,
                                      &error)) {
            if (result) {
                result->error = error == ERROR_SUCCESS ? ERROR_FILE_NOT_FOUND : error;
            }
            return false;
        }
    }

    if (!LoadModuleSymbolsFromExactPdb(spec, module, foundPdbPath, result)) {
        return false;
    }

    if (identity) {
        *identity = currentIdentity;
    }
    return true;
}

bool VerifyLoadedPdbIdentity(const DirectHookSpec& spec,
                             HMODULE module,
                             const PdbIdentity& identity,
                             DbgHelpSymbolResolveResult* result) {
    IMAGEHLP_MODULEW64 moduleInfo{};
    DWORD64 base = reinterpret_cast<DWORD64>(module);
    if (!QueryLoadedPdbInfo(base, &moduleInfo)) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=PDB_LOADED_INFO_FAILED api=%s moduleName=%s module=0x%p "
                   L"error=%lu message=%s",
                   ApiName(spec), SafeString(spec.moduleName), module, error,
                   FormatLastError(error).c_str());
        if (result) {
            result->error = error;
        }
        return false;
    }

    bool matches = PdbInfoMatchesIdentity(moduleInfo, identity);
    LogMessage(L"hookdll", matches ? LogLevel::Info : LogLevel::Warning,
               matches ? L"event=PDB_LOADED_MATCH api=%s moduleName=%s module=0x%p pdb=%s guid=%s age=%lu loadedPdb=%s"
                       : L"event=PDB_LOADED_MISMATCH api=%s moduleName=%s module=0x%p pdb=%s guid=%s age=%lu loadedPdb=%s",
               ApiName(spec), SafeString(spec.moduleName), module,
               identity.pdbName.c_str(), GuidToString(identity.guid).c_str(),
               identity.age, moduleInfo.LoadedPdbName);

    if (!matches && result) {
        result->error = ERROR_INVALID_DATA;
    }
    return matches;
}

bool TryAcceptSymbolAddress(const DirectHookSpec& spec,
                            HMODULE module,
                            DWORD64 address,
                            DbgHelpSymbolResolveResult* result) {
    std::uintptr_t rva = 0;
    if (!TryComputeRva(module, address, &rva)) {
        LogMessage(L"hookdll", LogLevel::Warning,
                   L"event=DBGHELP_SYMBOL_REJECTED api=%s moduleName=%s "
                   L"symbol=%s address=0x%llX reason=outside-module",
                   ApiName(spec), SafeString(spec.moduleName), SafeString(spec.symbolName),
                   address);
        if (result) {
            result->error = ERROR_INVALID_DATA;
        }
        return false;
    }

    if (result) {
        result->address = reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
        result->rva = rva;
        result->error = ERROR_SUCCESS;
    }
    return true;
}

bool TryQuerySymbol(const DirectHookSpec& spec,
                    const std::wstring& query,
                    HMODULE module,
                    const PdbIdentity& identity,
                    DbgHelpSymbolResolveResult* result) {
    constexpr ULONG kMaxSymbolName = 1024;
    std::array<unsigned char, sizeof(SYMBOL_INFOW) +
                                  kMaxSymbolName * sizeof(wchar_t)>
        symbolBuffer{};
    auto* symbol = reinterpret_cast<PSYMBOL_INFOW>(symbolBuffer.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
    symbol->MaxNameLen = kMaxSymbolName;

    if (!SymFromNameW(g_symbolSession, query.c_str(), symbol)) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=DBGHELP_SYMBOL_MISS api=%s moduleName=%s symbol=%s "
                   L"query=%s error=%lu",
                   ApiName(spec), SafeString(spec.moduleName), SafeString(spec.symbolName),
                   query.c_str(), error);
        if (result && result->error == ERROR_SUCCESS) {
            result->error = error;
        }
        return false;
    }

    if (!VerifyLoadedPdbIdentity(spec, module, identity, result)) {
        return false;
    }

    if (!TryAcceptSymbolAddress(spec, module, symbol->Address, result)) {
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=DBGHELP_SYMBOL_HIT api=%s moduleName=%s symbol=%s "
               L"query=%s address=0x%llX rva=0x%Ix",
               ApiName(spec), SafeString(spec.moduleName), SafeString(spec.symbolName),
               query.c_str(), symbol->Address, result ? result->rva : 0);
    return true;
}

BOOL CALLBACK EnumSymbolCallback(PSYMBOL_INFOW symbol, ULONG symbolSize, PVOID userContext) {
    (void)symbolSize;
    auto* context = reinterpret_cast<SymbolMatchContext*>(userContext);
    if (!context || !context->spec || !symbol) {
        return TRUE;
    }

    std::wstring_view name(symbol->Name, symbol->NameLen);
    if (!SymbolNameMatches(context->spec->symbolName, name)) {
        return TRUE;
    }

    std::uintptr_t rva = 0;
    if (!TryComputeRva(context->module, symbol->Address, &rva)) {
        return TRUE;
    }

    ++context->matchCount;
    context->matchAddress = symbol->Address;
    context->matchRva = rva;
    context->matchName.assign(name.data(), name.size());
    return TRUE;
}

bool TryEnumerateSymbol(const DirectHookSpec& spec,
                        HMODULE module,
                        const PdbIdentity& identity,
                        DbgHelpSymbolResolveResult* result) {
    DWORD64 base = reinterpret_cast<DWORD64>(module);
    std::wstring mask = BuildSymbolEnumMask(spec.symbolName);
    SymbolMatchContext context{};
    context.spec = &spec;
    context.module = module;

    if (!SymEnumSymbolsW(g_symbolSession, base, mask.c_str(), EnumSymbolCallback,
                         &context)) {
        DWORD error = GetLastError();
        LogMessage(L"hookdll", LogLevel::Info,
                   L"event=DBGHELP_SYMBOL_ENUM_FAILED api=%s moduleName=%s "
                   L"symbol=%s mask=%s error=%lu",
                   ApiName(spec), SafeString(spec.moduleName), SafeString(spec.symbolName),
                   mask.c_str(), error);
        if (result && result->error == ERROR_SUCCESS) {
            result->error = error;
        }
        return false;
    }

    if (context.matchCount != 1) {
        LogMessage(L"hookdll", context.matchCount == 0 ? LogLevel::Info : LogLevel::Warning,
                   L"event=DBGHELP_SYMBOL_ENUM_%s api=%s moduleName=%s symbol=%s "
                   L"mask=%s matchCount=%lu",
                   context.matchCount == 0 ? L"MISS" : L"AMBIGUOUS",
                   ApiName(spec), SafeString(spec.moduleName), SafeString(spec.symbolName),
                   mask.c_str(), context.matchCount);
        if (result && result->error == ERROR_SUCCESS) {
            result->error = context.matchCount == 0 ? ERROR_NOT_FOUND : ERROR_DUP_NAME;
        }
        return false;
    }

    if (!VerifyLoadedPdbIdentity(spec, module, identity, result)) {
        return false;
    }

    if (!TryAcceptSymbolAddress(spec, module, context.matchAddress, result)) {
        return false;
    }

    LogMessage(L"hookdll", LogLevel::Info,
               L"event=DBGHELP_SYMBOL_HIT api=%s moduleName=%s symbol=%s "
               L"query=enum:%s matched=%s address=0x%llX rva=0x%Ix",
               ApiName(spec), SafeString(spec.moduleName), SafeString(spec.symbolName),
               mask.c_str(), context.matchName.c_str(), context.matchAddress,
               context.matchRva);
    return true;
}

}  // namespace

bool TryResolveDbgHelpSymbolAddress(const DirectHookSpec& spec,
                                    HMODULE module,
                                    DbgHelpSymbolResolveResult* result) {
    if (result) {
        *result = DbgHelpSymbolResolveResult{};
    }

    std::lock_guard<std::mutex> lock(g_dbgHelpMutex);
    if (!InitializeDbgHelpSession()) {
        if (result && result->error == ERROR_SUCCESS) {
            result->error = GetLastError();
        }
        return false;
    }

    PdbIdentity identity{};
    if (!EnsureExactSymbolsLoaded(spec, module, &identity, result)) {
        return false;
    }

    std::vector<std::wstring> queries = BuildSymbolQueries(spec);
    for (const std::wstring& query : queries) {
        if (TryQuerySymbol(spec, query, module, identity, result)) {
            return true;
        }
    }

    return TryEnumerateSymbol(spec, module, identity, result);
}

}  // namespace touchrev
