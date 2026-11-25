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

include("${CMAKE_CURRENT_LIST_DIR}/cmake/DavidianSetup.cmake")

project(davidian_plugin_base VERSION 0.1.0 LANGUAGES C CXX)

# Top level: this repository on its own (Example Gain and the base tests).
# Otherwise: a submodule of a plugin repository, which has already called davidian_setup().
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(DAVIDIAN_BASE_TOP_LEVEL ON)
else()
  set(DAVIDIAN_BASE_TOP_LEVEL OFF)
endif()
option(DAVIDIAN_BASE_EXAMPLES "Build plugins/ (Example Gain) and the base's own tests" ${DAVIDIAN_BASE_TOP_LEVEL})
davidian_setup()

# ---------------------------------------------------------------- DPF

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dpf/CMakeLists.txt")
  message(FATAL_ERROR "dpf/ is empty: run  git submodule update --init --recursive")
endif()
set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)

# Fixes for DPF bugs not yet fixed upstream live in patches/*.patch (see patches/README.md).
# They are applied to a copy of DPF in the build tree, so the dpf/ submodule stays pinned
# and unmodified. The copy is redone whenever the DPF commit or a patch changes.
option(DAVIDIAN_DPF_PATCHES "Build from a copy of DPF with patches/*.patch applied" ON)
file(GLOB dpf_patches CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch")
list(SORT dpf_patches)
set(DAVIDIAN_DPF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dpf")
if(DAVIDIAN_DPF_PATCHES AND dpf_patches)
  find_package(Git REQUIRED)
  set(DAVIDIAN_DPF_DIR "${CMAKE_BINARY_DIR}/dpf-patched")
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}/dpf" rev-parse HEAD
    OUTPUT_VARIABLE dpf_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  set(stamp "dpf ${dpf_commit}\n")
  foreach(p IN LISTS dpf_patches)
    file(SHA256 "${p}" h)
    get_filename_component(n "${p}" NAME)
    string(APPEND stamp "${n} ${h}\n")
  endforeach()
  set(stamp_file "${DAVIDIAN_DPF_DIR}.stamp")
  set(old_stamp "")
  if(EXISTS "${stamp_file}")
    file(READ "${stamp_file}" old_stamp)
  endif()
  if(NOT old_stamp STREQUAL stamp OR NOT EXISTS "${DAVIDIAN_DPF_DIR}/CMakeLists.txt")
    message(STATUS "Patching a copy of DPF ${dpf_commit}")
    file(REMOVE_RECURSE "${DAVIDIAN_DPF_DIR}" "${stamp_file}")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/dpf/" DESTINATION "${DAVIDIAN_DPF_DIR}" PATTERN ".git" EXCLUDE)
    foreach(p IN LISTS dpf_patches)
      # The ceiling keeps git from treating an enclosing repository as the patch target.
      execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
                "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${p}"
        WORKING_DIRECTORY "${DAVIDIAN_DPF_DIR}"
        RESULT_VARIABLE rc ERROR_VARIABLE err)
      if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Patch ${p} does not apply to DPF ${dpf_commit}:\n${err}\n"
                            "If DPF was bumped and now includes the fix, delete the patch.")
      endif()
      message(STATUS "  applied ${p}")
    endforeach()
    file(WRITE "${stamp_file}" "${stamp}")
  endif()
endif()
add_subdirectory("${DAVIDIAN_DPF_DIR}" dpf)

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

# One line per plugin, written by davidian_add_plugin(), read by the ci/ scripts. It lives
# in the top-level build folder, also when this repository is a submodule.
file(WRITE "${CMAKE_BINARY_DIR}/plugins.tsv" "")

# ------------------------------------------------------------- tests

if(DAVIDIAN_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(DAVIDIAN_BASE_EXAMPLES)
  file(GLOB plugin_dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*")
  foreach(dir IN LISTS plugin_dirs)
    if(EXISTS "${dir}/CMakeLists.txt")
      add_subdirectory("${dir}")
    endif()
  endforeach()
endif()

