cmake_minimum_required(VERSION 3.25)

project(ncm_unblock_lite VERSION 0.1.2 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "ncm-unblock-lite currently supports Windows only")
endif()

if(NOT MSVC)
  message(FATAL_ERROR "ncm-unblock-lite currently requires the MSVC toolchain")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "ncm-unblock-lite currently requires an x64 toolchain")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(ncm_project_options INTERFACE)
target_compile_features(ncm_project_options INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(ncm_project_options INTERFACE /W4 /permissive- /utf-8 /EHsc)
  target_compile_definitions(ncm_project_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

include(CTest)

add_subdirectory(src/launcher)
add_subdirectory(src/host)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# Primary distribution is tools/package.ps1 → UnblockLite.plugin (zip).
# These install rules mirror the .plugin root layout for cmake --install consumers.
install(FILES README.md DESTINATION . COMPONENT Runtime)
install(
  FILES
    plugin/manifest.json
    plugin/main.js
    core/README.txt
  DESTINATION .
  COMPONENT Runtime
)
install(
  FILES core/README.txt
  DESTINATION core
  COMPONENT Runtime
)
