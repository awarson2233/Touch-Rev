if(NOT WIN32)
    message(FATAL_ERROR "TouchRev blocker only supports Windows.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "TouchRev blocker requires the MSVC toolchain for ARM64 or x64.")
endif()

set(TOUCHREV_BLOCKER_PLATFORM "")
if(DEFINED CMAKE_VS_PLATFORM_NAME AND CMAKE_VS_PLATFORM_NAME)
    set(TOUCHREV_BLOCKER_PLATFORM "${CMAKE_VS_PLATFORM_NAME}")
elseif(DEFINED CMAKE_GENERATOR_PLATFORM AND CMAKE_GENERATOR_PLATFORM)
    set(TOUCHREV_BLOCKER_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
else()
    set(TOUCHREV_BLOCKER_PLATFORM "${CMAKE_SYSTEM_PROCESSOR}")
endif()

string(TOUPPER "${TOUCHREV_BLOCKER_PLATFORM}" TOUCHREV_BLOCKER_PLATFORM_UPPER)
if(TOUCHREV_BLOCKER_PLATFORM_UPPER STREQUAL "AMD64" OR
   TOUCHREV_BLOCKER_PLATFORM_UPPER STREQUAL "X86_64")
    set(TOUCHREV_BLOCKER_PLATFORM_UPPER "X64")
endif()

if(NOT TOUCHREV_BLOCKER_PLATFORM_UPPER STREQUAL "ARM64" AND
   NOT TOUCHREV_BLOCKER_PLATFORM_UPPER STREQUAL "X64")
    message(FATAL_ERROR "TouchRev blocker must be configured for ARM64 or x64.")
endif()

add_library(touchrev_blocker_common STATIC
    common/log.cpp
    common/winutil.cpp
)
target_include_directories(touchrev_blocker_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(touchrev_blocker_common PUBLIC user32)
target_compile_options(touchrev_blocker_common PRIVATE /W4 /permissive- /EHsc)

if(TOUCHREV_BLOCKER_PLATFORM_UPPER STREQUAL "ARM64")
    target_compile_definitions(touchrev_blocker_common PUBLIC TOUCHREV_ARCH_ARM64)
elseif(TOUCHREV_BLOCKER_PLATFORM_UPPER STREQUAL "X64")
    target_compile_definitions(touchrev_blocker_common PUBLIC TOUCHREV_ARCH_X64)
endif()

add_library(TouchRevBlockerHook SHARED
    hookdll/dbghelp_symbol_provider.cpp
    hookdll/dllmain.cpp
    hookdll/direct_hook_resolver.cpp
    hookdll/twinui_gesture_table_patch.cpp
)
target_link_libraries(TouchRevBlockerHook PRIVATE touchrev_blocker_common user32 dbghelp winhttp)
target_compile_options(TouchRevBlockerHook PRIVATE /W4 /permissive- /EHsc)
target_compile_definitions(TouchRevBlockerHook PRIVATE TOUCHREV_HOOKDLL_BUILD)

set_target_properties(TouchRevBlockerHook PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:TouchRevGUI>"
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:TouchRevGUI>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/blocker"
)

target_compile_features(touchrev_blocker_common PRIVATE cxx_std_20)
target_compile_features(TouchRevBlockerHook PRIVATE cxx_std_20)

