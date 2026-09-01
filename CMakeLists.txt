cmake_minimum_required(VERSION 3.25)

project(ncm_unblock_297 VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "ncm-unblock-297 currently supports Windows only")
endif()

if(NOT MSVC)
  message(FATAL_ERROR "ncm-unblock-297 currently requires the MSVC toolchain")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "ncm-unblock-297 currently requires a Win32 toolchain")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(ncm_project_options INTERFACE)
target_compile_features(ncm_project_options INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(ncm_project_options INTERFACE /W4 /permissive- /utf-8 /EHsc)
  target_compile_definitions(ncm_project_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

include(CTest)

add_subdirectory(src/runtime_probe)
add_subdirectory(src/module_load_probe)
add_subdirectory(src/proxy_observer)
add_subdirectory(src/launcher)
add_subdirectory(src/bootstrap)
add_subdirectory(src/config)
add_subdirectory(src/winmm_proxy)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
