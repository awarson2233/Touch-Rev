cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(TouchRevGUI LANGUAGES CXX)

add_executable(TouchRevGUI WIN32
    src/main.cpp
    src/ui/AppWindow.cpp
    src/ui/AppWindow.h
    src/ui/AnimationSystem.cpp
    src/ui/AnimationSystem.h
    src/ui/BackdropController.cpp
    src/ui/BackdropController.h
    src/graphics/CompositionHost.cpp
    src/graphics/CompositionHost.h
    src/common/CoordinateSpace.h
    src/graphics/GraphicsDevice.cpp
    src/graphics/GraphicsDevice.h
    src/input/InputController.cpp
    src/input/InputController.h
    src/model/RectangleModel.h
    src/graphics/Renderer.cpp
    src/graphics/Renderer.h
    src/common/Win32Error.cpp
    src/common/Win32Error.h
)

target_compile_features(TouchRevGUI PRIVATE cxx_std_20)

target_include_directories(TouchRevGUI PRIVATE src)

target_compile_definitions(TouchRevGUI PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)

if(MSVC)
    target_compile_options(TouchRevGUI PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()

if(MINGW)
    target_link_options(TouchRevGUI PRIVATE -municode)
endif()

target_link_libraries(TouchRevGUI PRIVATE
    user32
    gdi32
    dwmapi
    d3d11
    dxgi
    d2d1
    dcomp
    ole32
    advapi32
)

if(MSVC)
    target_link_libraries(TouchRevGUI PRIVATE uianimation)
else()
    target_link_libraries(TouchRevGUI PRIVATE uuid)
endif()

set_property(TARGET TouchRevGUI PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
