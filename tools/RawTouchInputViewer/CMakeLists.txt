add_executable(RawTouchInputViewer WIN32
    RawTouchInputViewer.cpp
)

target_compile_features(RawTouchInputViewer PRIVATE cxx_std_20)
target_compile_definitions(RawTouchInputViewer PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)

if(MSVC)
    target_compile_options(RawTouchInputViewer PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(RawTouchInputViewer PRIVATE -Wno-nonportable-include-path)
endif()

target_link_libraries(RawTouchInputViewer PRIVATE
    touchrev_core
)

if(MINGW)
    target_link_options(RawTouchInputViewer PRIVATE -municode)
endif()

if(NOT MSVC)
    target_link_libraries(RawTouchInputViewer PRIVATE uuid)
endif()
