# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Build settings shared by davidian-plugin-base and every plugin repository that uses it
# as a submodule. A plugin repository's top-level CMakeLists.txt:
#
#   cmake_minimum_required(VERSION 3.17)
#   include(base/cmake/DavidianSetup.cmake)   # before project(): platform settings
#   project(my_plugin VERSION 1.0.0 LANGUAGES C CXX)
#   davidian_setup()                          # after project(): flags for every target
#   enable_testing()
#   add_subdirectory(base)                    # DPF, common/, davidian_add_plugin()
#   add_subdirectory(plugin)                  # one davidian_add_plugin() call
#
# Options (cache variables, set with -D):
#   DAVIDIAN_BUILD_TESTS  ON   tests and test tools
#   DAVIDIAN_WERROR       OFF  warnings in Davidian code are errors
#   DAVIDIAN_SANITIZE     ""   e.g. address,undefined: every target, DPF and plugins too
#   DAVIDIAN_RTSAN        OFF  Clang 20+: harness under RealtimeSanitizer
#   DAVIDIAN_DPF_PATCHES  ON   build DPF with base/patches/*.patch applied

include_guard(GLOBAL)

# ---- before project()
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS supported")
endif()
# Static C/C++ runtime with MSVC, so hosts and the standalone need no redistributable.
# (DPF's OpenGL UI builds with MinGW on Windows; davidian_setup() does the same there.)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DAVIDIAN_BUILD_TESTS "Build the tests and test tools" ON)
option(DAVIDIAN_WERROR "Treat warnings in Davidian code as errors" OFF)
set(DAVIDIAN_SANITIZE "" CACHE STRING "Sanitizers for every target, e.g. address,undefined")
option(DAVIDIAN_RTSAN "Build the harness with RealtimeSanitizer (Clang 20+)" OFF)

# ---- after project(): a macro, so the settings apply to the caller's whole tree
macro(davidian_setup)
  if(NOT DAVIDIAN_SETUP_DONE)
    set(DAVIDIAN_SETUP_DONE TRUE)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # MinGW (the Windows toolchain DPF's OpenGL UI supports): link the GCC runtime
    # statically, so nothing depends on libgcc / libstdc++ / libwinpthread DLLs.
    if(MINGW)
      add_link_options(-static -static-libgcc -static-libstdc++)
    endif()
    if(DAVIDIAN_SANITIZE)
      add_compile_options(-fsanitize=${DAVIDIAN_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
      add_link_options(-fsanitize=${DAVIDIAN_SANITIZE})
    endif()
  endif()
endmacro()
