cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(PrivateThumbnailProbe LANGUAGES CXX RC)

add_executable(PrivateThumbnailProbe
    src/main.cpp
    app.rc
    app.manifest
)

target_compile_features(PrivateThumbnailProbe PRIVATE cxx_std_20)

target_compile_definitions(PrivateThumbnailProbe PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)

set(WINDOWS_SDK_INCLUDE_ROOT "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0")
if(EXISTS "${WINDOWS_SDK_INCLUDE_ROOT}/cppwinrt/winrt/base.h")
    target_include_directories(PrivateThumbnailProbe PRIVATE
        "${WINDOWS_SDK_INCLUDE_ROOT}/cppwinrt"
    )
endif()

if(MSVC)
    target_compile_options(PrivateThumbnailProbe PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
else()
    target_compile_options(PrivateThumbnailProbe PRIVATE -Wall -Wextra -Wpedantic -Wno-nonportable-include-path)
endif()

if(MINGW)
    target_link_options(PrivateThumbnailProbe PRIVATE -municode)
endif()

target_link_libraries(PrivateThumbnailProbe PRIVATE
    ole32
    runtimeobject
    user32
)

if(MINGW)
    target_link_libraries(PrivateThumbnailProbe PRIVATE uuid)
endif()
