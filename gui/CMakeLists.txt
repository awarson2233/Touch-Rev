add_executable(TouchRevGUI WIN32
    main.cpp
    TouchRevGUI.rc
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/ConfigWindow.cpp
    ui/ConfigWindow.h
    ui/CardView.cpp
    ui/CardView.h
    ui/LayoutEngine.cpp
    ui/LayoutEngine.h
    ui/WindowController.cpp
    ui/WindowController.h
    ui/MainView.cpp
    ui/MainView.h
    ui/ThinXamlAppSwitcherHost.cpp
    ui/ThinXamlAppSwitcherHost.h
    ui/ThemeManager.cpp
    ui/ThemeManager.h
    thumbnail/PrivateThumbnailInterfaces.h
    thumbnail/PrivateThumbnailManager.cpp
    thumbnail/PrivateThumbnailManager.h
)

target_compile_features(TouchRevGUI PRIVATE cxx_std_20)

if(MSVC)
    set_target_properties(TouchRevGUI PROPERTIES 
        MSVC_MANIFEST OFF
        LINK_FLAGS "/MANIFEST:NO"
    )
endif()

target_include_directories(TouchRevGUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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
    touchrev_core
    Win32Acrylic::composition
)

if(NOT MSVC)
    target_link_libraries(TouchRevGUI PRIVATE uuid)
endif()

add_custom_command(TARGET TouchRevGUI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TouchRevGUI>/xaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/xaml/AppSwitcherRoot.xaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/xaml/SwitcherItem.xaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/xaml/ConfigRoot.xaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/xaml/ThemeResources.xaml"
        "$<TARGET_FILE_DIR:TouchRevGUI>/xaml"
    COMMENT "Copying XAML resources to TouchRevGUI target directory..."
)

if(TOUCHREV_BUILD_BLOCKER)
    target_compile_definitions(TouchRevGUI PRIVATE TOUCHREV_BUILD_BLOCKER)
    target_sources(TouchRevGUI PRIVATE
        ${CMAKE_SOURCE_DIR}/blocker/injector/inject.cpp
        ${CMAKE_SOURCE_DIR}/blocker/injector/inject.h
        ${CMAKE_SOURCE_DIR}/blocker/injector/process_find.cpp
        ${CMAKE_SOURCE_DIR}/blocker/injector/process_find.h
    )
    target_include_directories(TouchRevGUI PRIVATE ${CMAKE_SOURCE_DIR}/blocker)
    target_link_libraries(TouchRevGUI PRIVATE touchrev_blocker_common)
endif()

set_property(TARGET TouchRevGUI PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
