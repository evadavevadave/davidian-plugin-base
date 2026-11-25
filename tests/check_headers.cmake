# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Enforces the CLAUDE.md rule that shared DSP stays free of plugin-format and GUI code.
#   cmake -DCOMMON_DIR=<repo>/common -P tests/check_headers.cmake
#
# DSP headers (common/*.h, common/*.c*) may include only standard C/C++ headers, the
# x86 intrinsics header used for flush-to-zero, and each other. They must not include
# anything from common/ui/, DPF, LV2, VST3, CLAP, AU or a windowing / graphics system.
# common/ui/ may include DSP-free standard headers and other common/ui/ headers only.

if(NOT COMMON_DIR)
  message(FATAL_ERROR "pass -DCOMMON_DIR=...")
endif()

set(ALLOWED_SYSTEM
  math.h stdint.h stddef.h stdlib.h string.h stdio.h float.h limits.h xmmintrin.h
  cmath cstdint cstddef cstdlib cstring cstdio cfloat climits)

set(errors "")

function(check_file path)
  file(STRINGS "${path}" lines REGEX "^[ \t]*#[ \t]*include")
  foreach(line IN LISTS lines)
    string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*$" "\\1" inc "${line}")
    if(line MATCHES "<")
      list(FIND ALLOWED_SYSTEM "${inc}" idx)
      if(idx EQUAL -1)
        set(errors "${errors}\n  ${path}: system header <${inc}> is not allowed here")
      endif()
    else()
      get_filename_component(dir "${path}" DIRECTORY)
      if(NOT EXISTS "${dir}/${inc}")
        set(errors "${errors}\n  ${path}: \"${inc}\" is not a sibling header")
      elseif(inc MATCHES "/")
        set(errors "${errors}\n  ${path}: \"${inc}\" reaches outside its folder")
      endif()
    endif()
  endforeach()
  set(errors "${errors}" PARENT_SCOPE)
endfunction()

file(GLOB dsp_files "${COMMON_DIR}/*.h" "${COMMON_DIR}/*.c" "${COMMON_DIR}/*.cpp")
file(GLOB ui_files "${COMMON_DIR}/ui/*.h")

if(NOT dsp_files)
  message(FATAL_ERROR "no DSP headers found in ${COMMON_DIR}")
endif()

foreach(f IN LISTS dsp_files)
  check_file("${f}")
endforeach()
foreach(f IN LISTS ui_files)
  check_file("${f}")
endforeach()

list(LENGTH dsp_files nd)
list(LENGTH ui_files nu)
if(errors)
  message(FATAL_ERROR "Header rule violations:${errors}")
endif()
message(STATUS "check_headers: ${nd} DSP and ${nu} UI files clean")
