cmake_minimum_required(VERSION 3.20)
project(GraphicDriverGuard VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(uvdg_core STATIC
    native/src/config.cpp
    native/src/driver_version.cpp
    native/src/dx11_probe.cpp
    native/src/dx12_probe.cpp
    native/src/localization.cpp
    native/src/preflight.cpp
    native/src/vulkan_probe.cpp)
target_include_directories(uvdg_core PUBLIC native/include)
set_target_properties(uvdg_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    target_sources(uvdg_core PRIVATE native/src/driver_version_windows.cpp)
    target_link_libraries(uvdg_core PUBLIC advapi32 user32)
    add_library(UnityPlayerProxy SHARED
        native/src/dialog_windows.cpp
        native/src/proxy_windows.cpp
        native/windows/UnityPlayerProxy.def)
    target_link_libraries(UnityPlayerProxy PRIVATE uvdg_core advapi32 comctl32 shell32 user32)
    set_target_properties(UnityPlayerProxy PROPERTIES
        OUTPUT_NAME UnityPlayer
        PREFIX "")
elseif(UNIX AND NOT APPLE)
    target_link_libraries(uvdg_core PUBLIC dl)
    add_library(UnityPlayerProxy SHARED
        native/src/dialog_linux.cpp
        native/src/proxy_linux.cpp)
    target_link_libraries(UnityPlayerProxy PRIVATE uvdg_core)
    set_target_properties(UnityPlayerProxy PROPERTIES
        OUTPUT_NAME UnityPlayer
        PREFIX "")
else()
    message(FATAL_ERROR "Only Windows and Linux are supported")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(uvdg_tests
        tests/main.cpp
        tests/config_tests.cpp
        tests/driver_version_tests.cpp)
    target_link_libraries(uvdg_tests PRIVATE uvdg_core)
    add_test(NAME uvdg_tests COMMAND uvdg_tests)
endif()
