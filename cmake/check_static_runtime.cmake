# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Windows: every built binary (standalone .exe, VST3, CLAP, LV2 DLLs) must link the
# compiler runtime statically, so nothing needs an installer or a redistributable.
#   cmake -DOBJDUMP=<objdump> -DBIN=<build>/bin -DNAME=<name> -P check_static_runtime.cmake
#
# Fails on imports of the MinGW runtime (libgcc_s, libstdc++, libwinpthread) or the MSVC
# runtime (vcruntime, msvcp, ucrtbased). System DLLs (KERNEL32, USER32, OPENGL32, the
# api-ms-win-crt-* forwarders, ...) are fine.

if(NOT OBJDUMP OR NOT BIN OR NOT NAME)
  message(FATAL_ERROR "check_static_runtime: pass -DOBJDUMP= -DBIN= -DNAME=")
endif()

file(GLOB_RECURSE files
  "${BIN}/${NAME}.exe"
  "${BIN}/${NAME}.clap"
  "${BIN}/${NAME}.vst3/*.vst3"
  "${BIN}/${NAME}.lv2/*.dll")
list(FILTER files EXCLUDE REGEX "/$")

if(NOT files)
  message(FATAL_ERROR "check_static_runtime: no Windows binaries for ${NAME} in ${BIN}")
endif()

set(bad "")
foreach(f IN LISTS files)
  if(IS_DIRECTORY "${f}")
    continue()
  endif()
  execute_process(COMMAND "${OBJDUMP}" -p "${f}" OUTPUT_VARIABLE out RESULT_VARIABLE rc ERROR_QUIET)
  if(NOT rc EQUAL 0)
    string(APPEND bad "\n  ${f}: objdump failed")
    continue()
  endif()
  string(REGEX MATCHALL "DLL Name: [^\r\n]+" dlls "${out}")
  set(names "")
  foreach(d IN LISTS dlls)
    string(REGEX REPLACE "DLL Name: " "" d "${d}")
    string(STRIP "${d}" d)
    list(APPEND names "${d}")
    string(TOLOWER "${d}" dl)
    if(dl MATCHES "^(libgcc_s|libstdc\\+\\+|libwinpthread|vcruntime|msvcp|ucrtbased)")
      string(APPEND bad "\n  ${f}: imports ${d}")
    endif()
  endforeach()
  string(REPLACE ";" " " names "${names}")
  message(STATUS "${f}: ${names}")
endforeach()

if(bad)
  message(FATAL_ERROR "${NAME}: runtime DLLs would be needed at run time:${bad}")
endif()
list(LENGTH files n)
message(STATUS "${NAME}: ${n} Windows binaries, compiler runtime linked statically")
