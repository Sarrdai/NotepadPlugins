cmake_minimum_required(VERSION 3.21)
project(JsonPlusPlus VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JPP_BUILD_TESTS "Build the unit tests of the JSON core" ON)

# Platform independent JSON engine (unit tested on every platform).
add_library(jpp_core STATIC src/core/JsonCore.cpp)
target_include_directories(jpp_core PUBLIC src/core)

if(MSVC)
    # Static CRT: the plugin DLL has no runtime dependencies.
    set_property(TARGET jpp_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(jpp_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(jpp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    enable_language(RC)
    add_library(JsonPlusPlus SHARED
        src/plugin/PluginMain.cpp
        src/plugin/PrettyPanel.cpp
        src/plugin/Settings.cpp
        src/plugin/JsonPlusPlus.rc)
    target_include_directories(JsonPlusPlus PRIVATE src/plugin third_party/npp)
    target_compile_definitions(JsonPlusPlus PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(JsonPlusPlus PRIVATE jpp_core user32 gdi32)
    set_target_properties(JsonPlusPlus PROPERTIES PREFIX "" OUTPUT_NAME "JsonPlusPlus")
    if(MSVC)
        set_property(TARGET JsonPlusPlus PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(JsonPlusPlus PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(JsonPlusPlus PRIVATE -Wall -Wextra)
        target_link_options(JsonPlusPlus PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

if(JPP_BUILD_TESTS)
    enable_testing()
    add_executable(jpp_tests tests/JsonCoreTests.cpp)
    target_link_libraries(jpp_tests PRIVATE jpp_core)
    if(MSVC)
        set_property(TARGET jpp_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(jpp_tests PRIVATE /utf-8)
    endif()
    add_test(NAME jpp_core_tests COMMAND jpp_tests)
endif()
