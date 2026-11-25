# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Completes the LV2 metadata DPF generates, after each build:
#   SYMBOLS  replaces DPF's audio port symbols ("lv2_audio_in_1", ...) with the symbols a
#            plugin was released with, in port order (inputs, then outputs)
#   RENAME   renames other symbols, e.g. DPF's fixed "lv2_enabled" and "lv2_latency"
#   EMAIL    adds the maintainer's e-mail (foaf:mbox), which DPF has no field for
#   cmake -DTTL=<bundle>/<name>_dsp.ttl [-DSYMBOLS=in,out] [-DRENAME=lv2_enabled=enabled,...]
#         [-DEMAIL=a@b.c] -P lv2_metadata.cmake

if(NOT EXISTS "${TTL}")
  message(FATAL_ERROR "lv2_metadata: ${TTL} not found")
endif()
file(READ "${TTL}" ttl)
# TTL statements end in ";", which CMake would take as list separators.
string(REPLACE ";" "@SC@" ttl "${ttl}")

# ---- maintainer e-mail
if(EMAIL)
  if(NOT EMAIL MATCHES "^[^@ <>\"]+@[^@ <>\"]+$")
    message(FATAL_ERROR "lv2_metadata: '${EMAIL}' is not an e-mail address")
  endif()
  if(NOT ttl MATCHES "foaf:mbox")
    # DPF writes:  doap:maintainer [ <newline> foaf:name "..." ; <newline> ...
    string(REGEX MATCH "doap:maintainer \\[[ \t\r\n]+foaf:name [^\r\n]*@SC@\n" block "${ttl}")
    if(NOT block)
      message(FATAL_ERROR "lv2_metadata: no doap:maintainer block in ${TTL}")
    endif()
    string(REPLACE "${block}" "${block}        foaf:mbox <mailto:${EMAIL}> @SC@\n" ttl "${ttl}")
  endif()
endif()

# ---- released audio port symbols
if(SYMBOLS)
  string(REPLACE "," ";" wanted "${SYMBOLS}")
  # DPF writes each audio port as  a lv2:InputPort, lv2:AudioPort ; lv2:index N ; lv2:symbol "lv2_..." ;
  string(REGEX MATCHALL "lv2:AudioPort @SC@[ \t\r\n]+lv2:index [0-9]+ @SC@[ \t\r\n]+lv2:symbol \"lv2_[A-Za-z0-9_]+\"" ports "${ttl}")
  list(LENGTH ports nports)
  list(LENGTH wanted nwanted)
  if(NOT nports EQUAL nwanted)
    message(FATAL_ERROR "lv2_metadata: ${TTL} has ${nports} audio ports, LV2_AUDIO_SYMBOLS gives ${nwanted}")
  endif()
  set(i 0)
  foreach(port IN LISTS ports)
    list(GET wanted ${i} sym)
    if(NOT sym MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
      message(FATAL_ERROR "lv2_metadata: '${sym}' is not a valid LV2 symbol")
    endif()
    string(REGEX REPLACE "lv2:symbol \"lv2_[A-Za-z0-9_]+\"" "lv2:symbol \"${sym}\"" new "${port}")
    string(REPLACE "${port}" "${new}" ttl "${ttl}")
    math(EXPR i "${i} + 1")
  endforeach()
endif()

# ---- other symbols
if(RENAME)
  string(REPLACE "," ";" pairs "${RENAME}")
  foreach(pair IN LISTS pairs)
    if(NOT pair MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=([A-Za-z_][A-Za-z0-9_]*)$")
      message(FATAL_ERROR "lv2_metadata: '${pair}' is not old=new")
    endif()
    set(from "${CMAKE_MATCH_1}")
    set(to "${CMAKE_MATCH_2}")
    string(FIND "${ttl}" "lv2:symbol \"${from}\"" at)
    if(at EQUAL -1)
      message(FATAL_ERROR "lv2_metadata: no port with symbol '${from}' in ${TTL}")
    endif()
    string(FIND "${ttl}" "lv2:symbol \"${to}\"" taken)
    if(NOT taken EQUAL -1)
      message(FATAL_ERROR "lv2_metadata: symbol '${to}' is already used in ${TTL}")
    endif()
    string(REPLACE "lv2:symbol \"${from}\"" "lv2:symbol \"${to}\"" ttl "${ttl}")
  endforeach()
endif()

string(REPLACE "@SC@" ";" ttl "${ttl}")
file(WRITE "${TTL}" "${ttl}")
