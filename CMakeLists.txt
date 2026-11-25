# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Davidian plugin base: shared DSP (common/), the house-style GUI (common/ui/), DPF and
# every plugin under plugins/, built from one tree.
#
#   git submodule update --init --recursive
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure
#
# Plugins land in build/bin/ (.lv2, .vst3, .clap, standalone; .component on macOS).

cmake_minimum_required(VERSION 3.17)

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS supported")
endif()
# Static C/C++ runtime with MSVC, so hosts and the standalone need no redistributable.
# (DPF's OpenGL UI builds with MinGW on Windows; the MINGW block below does the same there.)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(davidian_plugin_base VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MinGW (the Windows toolchain DPF's OpenGL UI supports): link the GCC runtime statically,
# so plugins don't depend on libgcc / libstdc++ / libwinpthread DLLs.
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

option(DAVIDIAN_BUILD_TESTS "Build the tests in tests/" ON)
option(DAVIDIAN_WERROR "Treat warnings in our own code as errors" OFF)
# Sanitizer builds (see .github/workflows/build.yml):
#   -DDAVIDIAN_SANITIZE=address,undefined   everything, including DPF and the plugins (GCC
#                                           or Clang); the tests then run under ASan/UBSan
#   -DDAVIDIAN_RTSAN=ON                     Clang 20+: the harness checks, with
#                                           RealtimeSanitizer, that processing and parameter
#                                           changes never allocate, lock or make system calls
set(DAVIDIAN_SANITIZE "" CACHE STRING "Sanitizers for every target, e.g. address,undefined")
option(DAVIDIAN_RTSAN "Build the harness with RealtimeSanitizer (Clang 20+)" OFF)
if(DAVIDIAN_SANITIZE)
  add_compile_options(-fsanitize=${DAVIDIAN_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${DAVIDIAN_SANITIZE})
endif()

# ---------------------------------------------------------------- DPF

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dpf/CMakeLists.txt")
  message(FATAL_ERROR "dpf/ is empty: run  git submodule update --init --recursive")
endif()
set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)
add_subdirectory(dpf)

# ------------------------------------------------------- shared code

# Warnings for code in this repository (not DPF). No -ffast-math anywhere: it lets the
# compiler drop NaN guards and isfinite() checks.
add_library(davidian_warnings INTERFACE)
if(MSVC)
  target_compile_options(davidian_warnings INTERFACE /W4)
  if(DAVIDIAN_WERROR)
    target_compile_options(davidian_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(davidian_warnings INTERFACE -Wall -Wextra -fno-math-errno)
  if(DAVIDIAN_WERROR)
    target_compile_options(davidian_warnings INTERFACE -Werror)
  endif()
endif()

# Shared DSP: header-only C99 that also compiles as C++. No format or GUI headers.
add_library(davidian_dsp INTERFACE)
add_library(davidian::dsp ALIAS davidian_dsp)
target_include_directories(davidian_dsp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/common")
if(NOT MSVC)
  target_link_libraries(davidian_dsp INTERFACE m)
endif()

# House-style GUI helpers (common/ui/): GUI code only, used by each plugin's UI file as
#   #include "ui/panel.h"
add_library(davidian_ui INTERFACE)
add_library(davidian::ui ALIAS davidian_ui)
target_include_directories(davidian_ui INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/common")
if(NOT MSVC)
  target_link_libraries(davidian_ui INTERFACE m)
endif()

# ----------------------------------------------------------- plugins

# Each plugins/<Name>/CMakeLists.txt makes one davidian_add_plugin() call, which builds
# LV2, VST3, CLAP, the standalone app and (macOS) AU: see cmake/DavidianPlugin.cmake.
include(cmake/DavidianPlugin.cmake)

if(DAVIDIAN_BUILD_TESTS)
  enable_testing()
  # lilv (the LV2 host library) lets the tests load each built LV2 bundle like a DAW does.
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(LILV IMPORTED_TARGET lilv-0)
  endif()
  if(NOT LILV_FOUND)
    message(STATUS "lilv not found: LV2 load tests are skipped")
  endif()
endif()

# One line per plugin, written by davidian_add_plugin(), read by the ci/ scripts.
file(WRITE "${PROJECT_BINARY_DIR}/plugins.tsv" "")
file(GLOB plugin_dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*")
foreach(dir IN LISTS plugin_dirs)
  if(EXISTS "${dir}/CMakeLists.txt")
    add_subdirectory("${dir}")
  endif()
endforeach()

# ------------------------------------------------------------- tests

if(DAVIDIAN_BUILD_TESTS)
  add_subdirectory(tests)
endif()
