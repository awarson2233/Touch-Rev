cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(TouchRevGUI LANGUAGES CXX RC)

if(MSVC)
    add_compile_options(/MP /FS)
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 统一设置全局编译输出目录为顶级二进制目录，支持多配置生成器（如 VS 自动追加 /Debug 或 /Release）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(TOUCHREV_BUILD_BLOCKER "Build TouchRev blocker hook and injector" OFF)
option(TOUCHREV_BUILD_TOOLS "Build TouchRev development tools" ON)

# 1. 编译第三方库
add_subdirectory(third_party/Win32Acrylic)

# 2. 编译核心算法静态库
add_subdirectory(core)

# 3. 编译主 GUI 应用程序
add_subdirectory(gui)

# 4. 编译开发调试工具
if(TOUCHREV_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# 5. 编译系统手势拦截钩子
if(TOUCHREV_BUILD_BLOCKER)
    add_subdirectory(blocker)
endif()
