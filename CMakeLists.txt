cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(TouchRevGUI LANGUAGES CXX RC)

add_executable(TouchRevGUI WIN32
    src/main.cpp
    app.rc
    app.manifest
    src/ui/AppWindow.cpp
    src/ui/AppWindow.h
    src/ui/AppSwitcherLayoutEngine.cpp
    src/ui/AppSwitcherLayoutEngine.h
    src/ui/AppSwitcherXamlView.cpp
    src/ui/AppSwitcherXamlView.h
    src/ui/BackdropController.cpp
    src/ui/BackdropController.h
    src/ui/ThinXamlAppSwitcherHost.cpp
    src/ui/ThinXamlAppSwitcherHost.h
    src/thumbnail/PrivateThumbnailInterfaces.h
    src/thumbnail/PrivateThumbnailManager.cpp
    src/thumbnail/PrivateThumbnailManager.h
    src/common/CoordinateSpace.h
    src/input/InputController.cpp
    src/input/InputController.h
    src/common/Win32Error.cpp
    src/common/Win32Error.h
)

target_compile_features(TouchRevGUI PRIVATE cxx_std_20)

target_include_directories(TouchRevGUI PRIVATE src)

set(WINDOWS_SDK_INCLUDE_ROOT "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0")
if(EXISTS "${WINDOWS_SDK_INCLUDE_ROOT}/cppwinrt/winrt/base.h")
    target_include_directories(TouchRevGUI PRIVATE
        "${WINDOWS_SDK_INCLUDE_ROOT}/cppwinrt"
    )
endif()

target_compile_definitions(TouchRevGUI PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)

if(MSVC)
    target_compile_options(TouchRevGUI PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(TouchRevGUI PRIVATE -Wno-nonportable-include-path)
endif()

if(MINGW)
    target_link_options(TouchRevGUI PRIVATE -municode)
endif()

target_link_libraries(TouchRevGUI PRIVATE
    user32
    gdi32
    dwmapi
    ole32
    runtimeobject
    advapi32
)

if(NOT MSVC)
    target_link_libraries(TouchRevGUI PRIVATE uuid)
endif()

add_custom_target(copy_touchrev_xaml ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TouchRevGUI>/xaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/xaml/AppSwitcherRoot.xaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/xaml/SwitcherItem.xaml"
        "$<TARGET_FILE_DIR:TouchRevGUI>/xaml"
)
add_dependencies(TouchRevGUI copy_touchrev_xaml)

set_property(TARGET TouchRevGUI PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
