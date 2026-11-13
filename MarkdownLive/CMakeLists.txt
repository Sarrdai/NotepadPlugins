cmake_minimum_required(VERSION 3.20)
project(MarkdownLive VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  # Static CRT for every target (mdtext is linked into the plugin DLL), no runtime dependencies.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8)
endif()

# ---- platform independent text logic + unit tests ----------------------------------------
add_library(mdtext STATIC src/md_text.cpp src/code_syntax.cpp)
if(NOT MSVC)
  target_compile_options(mdtext PRIVATE -Wall -Wextra)
endif()

if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_executable(md_text_tests tests/md_text_tests.cpp)
  target_link_libraries(md_text_tests PRIVATE mdtext)
  add_test(NAME md_text_tests COMMAND md_text_tests)
endif()

if(NOT WIN32)
  message(STATUS "Not targeting Windows: only the unit tests are built. "
                 "Build on Windows with Visual Studio (cmake -A x64) to get the plugin.")
  return()
endif()

# ---- WebView2 SDK (headers + loader DLL) --------------------------------------------------
set(WEBVIEW2_VERSION "1.0.2903.40" CACHE STRING "Microsoft.Web.WebView2 NuGet package version")
set(WEBVIEW2_DIR "${CMAKE_BINARY_DIR}/webview2-${WEBVIEW2_VERSION}")
if(NOT EXISTS "${WEBVIEW2_DIR}/build/native/include/WebView2.h")
  set(_nupkg "${CMAKE_BINARY_DIR}/webview2-${WEBVIEW2_VERSION}.zip")
  message(STATUS "Downloading WebView2 SDK ${WEBVIEW2_VERSION}")
  file(DOWNLOAD
    "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${WEBVIEW2_VERSION}/microsoft.web.webview2.${WEBVIEW2_VERSION}.nupkg"
    "${_nupkg}"
    EXPECTED_HASH SHA256=ef128016dd1e51c59178c827ed5b8aa3322c57afa8675d930f8109505542ad74
    STATUS _status)
  list(GET _status 0 _code)
  if(NOT _code EQUAL 0)
    message(FATAL_ERROR "WebView2 SDK download failed: ${_status}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${_nupkg}" DESTINATION "${WEBVIEW2_DIR}")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(WEBVIEW2_ARCH x64)
else()
  set(WEBVIEW2_ARCH x86)
endif()

# ---- plugin DLL ------------------------------------------------------------------------------
add_library(MarkdownLive SHARED
  src/plugin.cpp
  src/preview_panel.cpp
  src/tool_panel.cpp
  src/commands.cpp
  src/editor.cpp
  src/code_highlight.cpp
  src/util.cpp)
set_target_properties(MarkdownLive PROPERTIES PREFIX "" OUTPUT_NAME "MarkdownLive")
target_compile_definitions(MarkdownLive PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00)
target_include_directories(MarkdownLive SYSTEM PRIVATE "${WEBVIEW2_DIR}/build/native/include")
target_include_directories(MarkdownLive PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared/cpp")
target_link_libraries(MarkdownLive PRIVATE mdtext shlwapi gdiplus uxtheme ole32 comdlg32 shell32 user32 gdi32)

if(MSVC)
  target_compile_options(MarkdownLive PRIVATE /W4)
endif()

# ---- package: <build>/package/MarkdownLive is the folder to copy into Notepad++\plugins ----
set(PREVIEW_ASSETS "${CMAKE_SOURCE_DIR}/web/dist" CACHE PATH "Bundled preview page (npm run build in web/)")
set(PACKAGE_DIR "${CMAKE_BINARY_DIR}/package/MarkdownLive")
# With multi-config generators (Visual Studio) the package lands in <build>/package as well.
add_custom_target(package_plugin ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MarkdownLive>" "${PACKAGE_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${WEBVIEW2_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2Loader.dll" "${PACKAGE_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${WEBVIEW2_DIR}/LICENSE.txt" "${PACKAGE_DIR}/WebView2-LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PREVIEW_ASSETS}" "${PACKAGE_DIR}/assets"
  DEPENDS MarkdownLive
  COMMENT "Assembling plugin folder ${PACKAGE_DIR}"
  VERBATIM)
