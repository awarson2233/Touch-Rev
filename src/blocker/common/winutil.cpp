#include "common/winutil.h"

#include <winnt.h>

#include <cwchar>
#include <vector>

namespace touchrev {

std::wstring FormatLastError(DWORD error) {
    if (error == ERROR_SUCCESS) {
        return L"ERROR_SUCCESS";
    }

    PWSTR buffer = nullptr;
    DWORD chars = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                     FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, error,
                                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 reinterpret_cast<PWSTR>(&buffer), 0, nullptr);
    if (chars == 0 || !buffer) {
        return L"error=" + std::to_wstring(error);
    }

    std::wstring message(buffer, chars);
    LocalFree(buffer);

    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' ||
            message.back() == L' ' || message.back() == L'\t')) {
        message.pop_back();
    }

    return message;
}

std::wstring GetWindowClassNameSafe(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return {};
    }

    wchar_t className[256];
    int chars = GetClassNameW(hwnd, className, ARRAYSIZE(className));
    if (chars <= 0) {
        return {};
    }

    return std::wstring(className, chars);
}

bool WindowClassEquals(HWND hwnd, PCWSTR expectedClassName) {
    if (!expectedClassName) {
        return false;
    }

    std::wstring className = GetWindowClassNameSafe(hwnd);
    if (className.empty()) {
        return false;
    }

    return wcscmp(className.c_str(), expectedClassName) == 0;
}

bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    if (left.empty()) {
        return true;
    }

    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                                right.data(), static_cast<int>(right.size()),
                                TRUE) == CSTR_EQUAL;
}

std::wstring GetFileNameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return path;
    }

    return path.substr(pos + 1);
}

std::wstring GetFullPath(const std::wstring& path) {
    DWORD chars = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (chars == 0) {
        return {};
    }

    std::wstring fullPath(chars, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), chars, fullPath.data(), nullptr);
    if (written == 0 || written >= chars) {
        return {};
    }

    fullPath.resize(written);
    return fullPath;
}

bool GetPeMachineType(const std::wstring& path, USHORT* machine) {
    if (!machine) {
        return false;
    }

    *machine = IMAGE_FILE_MACHINE_UNKNOWN;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    IMAGE_DOS_HEADER dosHeader{};
    DWORD bytesRead = 0;
    bool ok = ReadFile(file, &dosHeader, sizeof(dosHeader), &bytesRead, nullptr) &&
              bytesRead == sizeof(dosHeader) &&
              dosHeader.e_magic == IMAGE_DOS_SIGNATURE;

    if (ok) {
        LARGE_INTEGER offset{};
        offset.QuadPart = dosHeader.e_lfanew;
        ok = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != FALSE;
    }

    DWORD signature = 0;
    if (ok) {
        ok = ReadFile(file, &signature, sizeof(signature), &bytesRead, nullptr) &&
             bytesRead == sizeof(signature) && signature == IMAGE_NT_SIGNATURE;
    }

    IMAGE_FILE_HEADER fileHeader{};
    if (ok) {
        ok = ReadFile(file, &fileHeader, sizeof(fileHeader), &bytesRead, nullptr) &&
             bytesRead == sizeof(fileHeader);
    }

    if (ok) {
        *machine = fileHeader.Machine;
    }

    CloseHandle(file);
    return ok;
}

USHORT CurrentBuildMachineType() {
#if defined(TOUCHREV_ARCH_ARM64)
    return IMAGE_FILE_MACHINE_ARM64;
#elif defined(TOUCHREV_ARCH_X64)
    return IMAGE_FILE_MACHINE_AMD64;
#else
#error TouchRevHook must be built for ARM64 or x64.
#endif
}

PCWSTR CurrentBuildArchName() {
#if defined(TOUCHREV_ARCH_ARM64)
    return L"ARM64";
#elif defined(TOUCHREV_ARCH_X64)
    return L"x64";
#else
#error TouchRevHook must be built for ARM64 or x64.
#endif
}

PCWSTR MachineTypeToString(USHORT machine) {
    switch (machine) {
    case IMAGE_FILE_MACHINE_UNKNOWN:
        return L"UNKNOWN";
    case IMAGE_FILE_MACHINE_I386:
        return L"I386";
    case IMAGE_FILE_MACHINE_AMD64:
        return L"AMD64";
    case IMAGE_FILE_MACHINE_ARM64:
        return L"ARM64";
#if defined(IMAGE_FILE_MACHINE_ARM64EC)
    case IMAGE_FILE_MACHINE_ARM64EC:
        return L"ARM64EC";
#endif
#if defined(IMAGE_FILE_MACHINE_ARM64X)
    case IMAGE_FILE_MACHINE_ARM64X:
        return L"ARM64X";
#endif
    case IMAGE_FILE_MACHINE_ARMNT:
        return L"ARMNT";
    default:
        return L"OTHER";
    }
}

}  // namespace touchrev
