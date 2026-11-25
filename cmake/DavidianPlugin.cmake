# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# davidian_add_plugin(<name>
#   FILES_DSP <files...>          plugin (DSP) sources
#   [FILES_UI <files...>]         UI sources; omit for a plugin without a GUI
#   [LV2_AUDIO_SYMBOLS <sym...>]  LV2 audio port symbols of an already-released plugin,
#                                 inputs then outputs (see below)
#   [TARGETS <formats...>])       default: lv2 vst3 clap jack au
#
# One dpf_add_plugin() call builds every format from the same code:
#   lv2   build/bin/<name>.lv2/            (DPF's LV2 wrapper and generated .ttl files)
#   vst3  build/bin/<name>.vst3/
#   clap  build/bin/<name>.clap
#   jack  build/bin/<name>[.exe]           standalone app: JACK when running, otherwise the
#                                          native audio system (CoreAudio, WASAPI/DirectSound,
#                                          ALSA or PulseAudio)
#   au    build/bin/<name>.component/      macOS only; DPF skips it on other systems
#
# Identity: URI, UI URI, CLAP ID, AU/VST3 brand and unique IDs come from the plugin's
# DistrhoPluginInfo.h. LV2 control ports follow the audio ports in parameter order and
# use the parameter symbols, so a port of an existing LV2 plugin keeps its indices and
# symbols by keeping its parameter order and symbols.
#
# DPF always names LV2 audio ports "lv2_<symbol>". LV2_AUDIO_SYMBOLS rewrites them in the
# generated .ttl after each build, so a plugin released with other audio symbols (for
# example "in" / "out") keeps them. The binary addresses ports by index, so only the
# metadata changes.
#
# Tests added for each plugin:
#   formats_<name>     every format for this platform was built
#   compat_<name>      if the folder holds compat.txt: URIs, IDs and LV2 ports still match
#                      the released ones (cmake/check_compat.cmake)
#   static_runtime_<name>  Windows: no binary needs the MinGW or MSVC runtime DLLs
#   lv2_<name>_<rate>  if lilv is installed: the LV2 bundle loads in a host, instantiates
#                      with default controls at 44.1 and 192 kHz and produces finite,
#                      non-silent audio (tests/lv2_host.c)
#
# davidian_add_lv2_test(<name> <test> [symbol=value ...] [--expect-gain G] [--rate R])
# adds a further LV2 host run with chosen control values and an optional gain check.

include_guard(GLOBAL)

set(DAVIDIAN_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(davidian_add_plugin NAME)
  cmake_parse_arguments(ARG "" "" "FILES_DSP;FILES_UI;LV2_AUDIO_SYMBOLS;TARGETS" ${ARGN})
  if(NOT ARG_FILES_DSP)
    message(FATAL_ERROR "davidian_add_plugin(${NAME}): FILES_DSP is required")
  endif()
  if(NOT ARG_TARGETS)
    set(ARG_TARGETS lv2 vst3 clap jack au)
  endif()

  if(ARG_FILES_UI)
    dpf_add_plugin(${NAME}
      TARGETS ${ARG_TARGETS}
      FILES_DSP ${ARG_FILES_DSP}
      FILES_UI ${ARG_FILES_UI})
  else()
    dpf_add_plugin(${NAME}
      TARGETS ${ARG_TARGETS}
      FILES_DSP ${ARG_FILES_DSP})
  endif()

  # DistrhoPluginInfo.h and the plugin's own headers, for both the DSP and the UI.
  target_include_directories(${NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${NAME}-dsp PRIVATE davidian::dsp davidian_warnings)
  if(ARG_FILES_UI)
    target_link_libraries(${NAME}-ui PRIVATE davidian::ui davidian_warnings)
  endif()

  set(bin "${PROJECT_BINARY_DIR}/bin")

  # The LV2 URI, for the host tests.
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/DistrhoPluginInfo.h" info)
  if(info MATCHES "#define[ \t]+DISTRHO_PLUGIN_URI[ \t]+\"([^\"]+)\"")
    set_property(GLOBAL PROPERTY DAVIDIAN_${NAME}_URI "${CMAKE_MATCH_1}")
  endif()

  if("lv2" IN_LIST ARG_TARGETS AND ARG_LV2_AUDIO_SYMBOLS)
    # Runs after DPF's own POST_BUILD step that generates the .ttl files.
    string(REPLACE ";" "," syms "${ARG_LV2_AUDIO_SYMBOLS}")
    add_custom_command(TARGET ${NAME}-lv2 POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -DTTL=${bin}/${NAME}.lv2/${NAME}_dsp.ttl -DSYMBOLS=${syms}
              -P "${DAVIDIAN_CMAKE_DIR}/lv2_audio_symbols.cmake"
      VERBATIM)
  endif()

  if(DAVIDIAN_BUILD_TESTS)
    string(REPLACE ";" "," targets "${ARG_TARGETS}")
    add_test(NAME formats_${NAME}
      COMMAND "${CMAKE_COMMAND}" -DBIN=${bin} -DNAME=${NAME} -DTARGETS=${targets}
              -DHAS_UI=$<BOOL:${ARG_FILES_UI}> -DAPPLE=${APPLE} -DWIN32=${WIN32}
              -P "${DAVIDIAN_CMAKE_DIR}/check_formats.cmake")
    if(WIN32 AND CMAKE_OBJDUMP)
      add_test(NAME static_runtime_${NAME}
        COMMAND "${CMAKE_COMMAND}" -DOBJDUMP=${CMAKE_OBJDUMP} -DBIN=${bin} -DNAME=${NAME}
                -P "${DAVIDIAN_CMAKE_DIR}/check_static_runtime.cmake")
    endif()
    if("lv2" IN_LIST ARG_TARGETS)
      foreach(rate 44100 192000)
        davidian_add_lv2_test(${NAME} lv2_${NAME}_${rate} --rate ${rate})
      endforeach()
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/compat.txt")
      add_test(NAME compat_${NAME}
        COMMAND "${CMAKE_COMMAND}" -DCOMPAT=${CMAKE_CURRENT_SOURCE_DIR}/compat.txt
                -DINFO=${CMAKE_CURRENT_SOURCE_DIR}/DistrhoPluginInfo.h
                -DBUNDLE=${bin}/${NAME}.lv2 -DNAME=${NAME}
                -P "${DAVIDIAN_CMAKE_DIR}/check_compat.cmake")
    endif()
  endif()
endfunction()

function(davidian_add_lv2_test NAME TEST)
  get_property(uri GLOBAL PROPERTY DAVIDIAN_${NAME}_URI)
  if(NOT DAVIDIAN_BUILD_TESTS OR NOT LILV_FOUND OR NOT uri)
    return()
  endif()
  add_test(NAME ${TEST}
    COMMAND lv2_host "${PROJECT_BINARY_DIR}/bin/${NAME}.lv2" "${uri}" ${ARGN})
endfunction()
