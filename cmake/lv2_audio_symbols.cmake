# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Replaces DPF's generated LV2 audio port symbols ("lv2_audio_in_1", ...) with the
# symbols a plugin was released with, in port order (inputs, then outputs).
#   cmake -DTTL=<bundle>/<name>.ttl -DSYMBOLS=in,out -P lv2_audio_symbols.cmake

if(NOT EXISTS "${TTL}")
  message(FATAL_ERROR "lv2_audio_symbols: ${TTL} not found")
endif()
string(REPLACE "," ";" wanted "${SYMBOLS}")
file(READ "${TTL}" ttl)
# TTL statements end in ";", which CMake would take as list separators.
string(REPLACE ";" "@SC@" ttl "${ttl}")

# DPF writes each audio port as  a lv2:InputPort, lv2:AudioPort ; lv2:index N ; lv2:symbol "lv2_..." ;
string(REGEX MATCHALL "lv2:AudioPort @SC@[ \t\r\n]+lv2:index [0-9]+ @SC@[ \t\r\n]+lv2:symbol \"lv2_[A-Za-z0-9_]+\"" ports "${ttl}")
list(LENGTH ports nports)
list(LENGTH wanted nwanted)
if(NOT nports EQUAL nwanted)
  message(FATAL_ERROR "lv2_audio_symbols: ${TTL} has ${nports} audio ports, LV2_AUDIO_SYMBOLS gives ${nwanted}")
endif()

set(i 0)
foreach(port IN LISTS ports)
  list(GET wanted ${i} sym)
  if(NOT sym MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "lv2_audio_symbols: '${sym}' is not a valid LV2 symbol")
  endif()
  string(REGEX REPLACE "lv2:symbol \"lv2_[A-Za-z0-9_]+\"" "lv2:symbol \"${sym}\"" new "${port}")
  string(REPLACE "${port}" "${new}" ttl "${ttl}")
  math(EXPR i "${i} + 1")
endforeach()

string(REPLACE "@SC@" ";" ttl "${ttl}")
file(WRITE "${TTL}" "${ttl}")
