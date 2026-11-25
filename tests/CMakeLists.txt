# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian

add_executable(test_dsp test_dsp.c)
target_link_libraries(test_dsp PRIVATE davidian::dsp davidian_warnings)
add_test(NAME dsp COMMAND test_dsp)

add_executable(test_panel test_panel.c)
target_link_libraries(test_panel PRIVATE davidian::ui davidian_warnings)
add_test(NAME panel COMMAND test_panel "${CMAKE_BINARY_DIR}/panel.bmp")

add_executable(test_cxx test_cxx.cpp)
target_link_libraries(test_cxx PRIVATE davidian::dsp davidian::ui davidian_warnings)
add_test(NAME cxx COMMAND test_cxx)

add_test(NAME dsp_headers_format_free
  COMMAND "${CMAKE_COMMAND}" -DCOMMON_DIR=${PROJECT_SOURCE_DIR}/common
          -P "${CMAKE_CURRENT_SOURCE_DIR}/check_headers.cmake")

# Minimal LV2 host for the per-plugin LV2 load tests (see cmake/DavidianPlugin.cmake).
if(LILV_FOUND)
  add_executable(lv2_host lv2_host.c)
  target_link_libraries(lv2_host PRIVATE PkgConfig::LILV davidian_warnings)
  if(NOT MSVC)
    target_link_libraries(lv2_host PRIVATE m)
  endif()
endif()

# Host for the per-plugin state restore tests (CLAP and VST3, loaded like a DAW does).
add_executable(plugin_state plugin_state.cpp)
target_include_directories(plugin_state PRIVATE "${DAVIDIAN_DPF_DIR}/distrho/src")
target_link_libraries(plugin_state PRIVATE ${CMAKE_DL_LIBS})
