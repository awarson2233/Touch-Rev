add_library(touchrev_core STATIC
    common/IconUtils.cpp
    common/IconUtils.h
    common/AppSettings.h
    common/CoordinateSpace.h
    common/DwmUtils.h
    common/FileUtils.h
    common/GeometryUtils.h
    common/PathUtils.h
    common/StringUtils.h
    common/Win32Error.cpp
    common/Win32Error.h
    common/XamlIslandCommon.h
    
    input/InputController.cpp
    input/InputController.h
    input/raw/RawTouchInput.cpp
    input/raw/RawTouchInput.h
    input/gesture/GestureContact.cpp
    input/gesture/GestureContact.h
    input/gesture/GestureContext.cpp
    input/gesture/GestureContext.h
    input/gesture/GestureRecognizers.cpp
    input/gesture/GestureRecognizers.h
    input/gesture/GestureSession.cpp
    input/gesture/GestureSession.h
    input/gesture/GestureTypes.h
    input/gesture/ThreeFingerGestureRecognizer.cpp
    input/gesture/ThreeFingerGestureRecognizer.h
)

target_compile_features(touchrev_core PRIVATE cxx_std_20)

target_include_directories(touchrev_core 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(touchrev_core PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)

if(MSVC)
    target_compile_options(touchrev_core PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(touchrev_core PRIVATE -Wno-nonportable-include-path)
endif()

target_link_libraries(touchrev_core PUBLIC
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
