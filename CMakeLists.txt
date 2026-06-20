cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(TouchRevGUI LANGUAGES CXX RC)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(TOUCHREV_BUILD_BLOCKER "Build TouchRev blocker hook and injector" OFF)
option(TOUCHREV_BUILD_TOOLS "Build TouchRev development tools" ON)

add_executable(TouchRevGUI WIN32
    src/main.cpp
    TouchRevGUI.rc
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ConfigWindow.cpp
    src/ui/ConfigWindow.h
    src/appswitcher/CardView.cpp
    src/appswitcher/CardView.h
    src/appswitcher/LayoutEngine.cpp
    src/appswitcher/LayoutEngine.h
    src/appswitcher/WindowController.cpp
    src/appswitcher/WindowController.h
    src/appswitcher/MainView.cpp
    src/appswitcher/MainView.h
    src/ui/ThinXamlAppSwitcherHost.cpp
    src/ui/ThinXamlAppSwitcherHost.h
    src/ui/ThemeManager.cpp
    src/ui/ThemeManager.h
    src/thumbnail/PrivateThumbnailInterfaces.h
    src/thumbnail/PrivateThumbnailManager.cpp
    src/thumbnail/PrivateThumbnailManager.h
    src/common/CoordinateSpace.h
    src/common/IconUtils.cpp
    src/common/IconUtils.h
    src/input/InputController.cpp
    src/input/InputController.h
    src/input/raw/RawTouchInput.cpp
    src/input/raw/RawTouchInput.h
    src/input/gesture/ThreeFingerGestureRecognizer.cpp
    src/input/gesture/ThreeFingerGestureRecognizer.h
    src/common/Win32Error.cpp
    src/common/Win32Error.h
)

target_compile_features(TouchRevGUI PRIVATE cxx_std_20)

if(MSVC)
    set_target_properties(TouchRevGUI PROPERTIES 
        MSVC_MANIFEST OFF
        LINK_FLAGS "/MANIFEST:NO"
    )
endif()

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
    shell32
    propsys
    hid
)

if(NOT MSVC)
    target_link_libraries(TouchRevGUI PRIVATE uuid)
endif()

if(TOUCHREV_BUILD_TOOLS)
    add_executable(RawTouchInputViewer WIN32
        src/tools/RawTouchInputViewer.cpp
        src/input/raw/RawTouchInput.cpp
        src/input/raw/RawTouchInput.h
        src/input/gesture/ThreeFingerGestureRecognizer.cpp
        src/input/gesture/ThreeFingerGestureRecognizer.h
        src/common/Win32Error.cpp
        src/common/Win32Error.h
    )

    target_compile_features(RawTouchInputViewer PRIVATE cxx_std_20)
    target_include_directories(RawTouchInputViewer PRIVATE src)
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
        user32
        gdi32
        hid
    )

    if(MINGW)
        target_link_options(RawTouchInputViewer PRIVATE -municode)
    endif()

    if(NOT MSVC)
        target_link_libraries(RawTouchInputViewer PRIVATE uuid)
    endif()
endif()

add_custom_target(copy_touchrev_xaml ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TouchRevGUI>/xaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/appswitcher/xaml/AppSwitcherRoot.xaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/appswitcher/xaml/SwitcherItem.xaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/appswitcher/xaml/ConfigRoot.xaml"
        "$<TARGET_FILE_DIR:TouchRevGUI>/xaml"
)
add_dependencies(TouchRevGUI copy_touchrev_xaml)

if(TOUCHREV_BUILD_BLOCKER)
    add_subdirectory(src/blocker)
    target_compile_definitions(TouchRevGUI PRIVATE TOUCHREV_BUILD_BLOCKER)
    target_sources(TouchRevGUI PRIVATE
        src/blocker/injector/inject.cpp
        src/blocker/injector/inject.h
        src/blocker/injector/process_find.cpp
        src/blocker/injector/process_find.h
    )
    target_include_directories(TouchRevGUI PRIVATE src/blocker)
    target_link_libraries(TouchRevGUI PRIVATE touchrev_blocker_common)
endif()

set_property(TARGET TouchRevGUI PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
