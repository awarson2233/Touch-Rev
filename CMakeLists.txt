cmake_minimum_required(VERSION 3.24)

project(TouchRevGUI LANGUAGES CXX)

add_executable(TouchRevGUI WIN32
    src/main.cpp
    src/AppWindow.cpp
    src/AppWindow.h
    src/AnimationSystem.cpp
    src/AnimationSystem.h
    src/BackdropController.cpp
    src/BackdropController.h
    src/CompositionHost.cpp
    src/CompositionHost.h
    src/CoordinateSpace.h
    src/GraphicsDevice.cpp
    src/GraphicsDevice.h
    src/InputController.cpp
    src/InputController.h
    src/RectangleModel.h
    src/Renderer.cpp
    src/Renderer.h
    src/Win32Error.cpp
    src/Win32Error.h
)

target_compile_features(TouchRevGUI PRIVATE cxx_std_20)

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
