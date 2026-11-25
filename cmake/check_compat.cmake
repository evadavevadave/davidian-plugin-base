# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Guards a released plugin's identity so existing sessions keep loading.
#   cmake -DCOMPAT=compat.txt -DINFO=DistrhoPluginInfo.h -DBUNDLE=<bin>/<name>.lv2 -DNAME=<name>
#         -P check_compat.cmake
#
# compat.txt records what was released; never edit it after a release. Lines:
#   uri        <LV2 plugin URI>             DISTRHO_PLUGIN_URI
#   ui_uri     <LV2 UI URI>                 DISTRHO_UI_URI
#   clap_id    <CLAP ID>                    DISTRHO_PLUGIN_CLAP_ID
#   brand_id   <4 chars>                    DISTRHO_PLUGIN_BRAND_ID  (AU manufacturer, VST3)
#   unique_id  <4 chars>                    DISTRHO_PLUGIN_UNIQUE_ID (AU subtype, VST3)
#   port       <index> <symbol>             one per LV2 port, audio and control
# Blank lines and lines starting with # are ignored.
#
# Checks: every identity line matches DistrhoPluginInfo.h; the generated LV2 bundle
# declares the URI and UI URI; every recorded port has the same index and symbol.
# Ports added after the recorded ones are allowed (reported), anything else fails.

foreach(v COMPAT INFO BUNDLE NAME)
  if(NOT ${v})
    message(FATAL_ERROR "check_compat: pass -D${v}=...")
  endif()
endforeach()

set(errors "")
file(STRINGS "${COMPAT}" lines)
file(READ "${INFO}" info)
file(READ "${BUNDLE}/manifest.ttl" manifest)
file(READ "${BUNDLE}/${NAME}_dsp.ttl" ttl)
# TTL statements end in ";", which CMake would take as list separators.
string(REPLACE ";" "@SC@" ttl "${ttl}")

# Value of a #define in DistrhoPluginInfo.h, with DISTRHO_PLUGIN_URI "x" concatenation
# resolved and quotes removed.
function(info_define name out)
  if(info MATCHES "#define[ \t]+${name}[ \t]+([^\r\n]*)")
    set(v "${CMAKE_MATCH_1}")
    string(REGEX REPLACE "[ \t]*//.*$" "" v "${v}")
    string(REGEX REPLACE "[ \t]*/\\*.*$" "" v "${v}")
    string(STRIP "${v}" v)
    if(v MATCHES "^DISTRHO_PLUGIN_URI[ \t]+\"([^\"]*)\"$")
      info_define(DISTRHO_PLUGIN_URI base)
      set(v "${base}${CMAKE_MATCH_1}")
    endif()
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" v "${v}")
    set(${out} "${v}" PARENT_SCOPE)
  else()
    set(${out} "" PARENT_SCOPE)
  endif()
endfunction()

# Index -> symbol map of the generated ttl (symbol follows index within one port block).
string(REGEX MATCHALL "lv2:index [0-9]+ @SC@[^]]*lv2:symbol \"[A-Za-z0-9_]+\"" gen_ports "${ttl}")
set(max_gen -1)
foreach(p IN LISTS gen_ports)
  string(REGEX MATCH "lv2:index ([0-9]+)" _ "${p}")
  set(idx "${CMAKE_MATCH_1}")
  string(REGEX MATCH "lv2:symbol \"([A-Za-z0-9_]+)\"" _ "${p}")
  set(gen_${idx} "${CMAKE_MATCH_1}")
  if(idx GREATER max_gen)
    set(max_gen ${idx})
  endif()
endforeach()

set(map_uri DISTRHO_PLUGIN_URI)
set(map_ui_uri DISTRHO_UI_URI)
set(map_clap_id DISTRHO_PLUGIN_CLAP_ID)
set(map_brand_id DISTRHO_PLUGIN_BRAND_ID)
set(map_unique_id DISTRHO_PLUGIN_UNIQUE_ID)

set(max_rec -1)
set(nports 0)
foreach(line IN LISTS lines)
  string(STRIP "${line}" line)
  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  endif()
  string(REGEX REPLACE "[ \t]+" ";" f "${line}")
  list(GET f 0 key)
  if(key STREQUAL "port")
    list(GET f 1 idx)
    list(GET f 2 sym)
    math(EXPR nports "${nports} + 1")
    if(idx GREATER max_rec)
      set(max_rec ${idx})
    endif()
    if(NOT DEFINED gen_${idx})
      string(APPEND errors "\n  port ${idx} '${sym}' is missing from ${NAME}_dsp.ttl")
    elseif(NOT gen_${idx} STREQUAL sym)
      string(APPEND errors "\n  port ${idx}: released as '${sym}', now '${gen_${idx}}'")
    endif()
  elseif(DEFINED map_${key})
    list(GET f 1 want)
    info_define(${map_${key}} have)
    if(NOT have STREQUAL want)
      string(APPEND errors "\n  ${map_${key}}: released as '${want}', DistrhoPluginInfo.h has '${have}'")
    endif()
    if(key STREQUAL "uri")
      string(FIND "${manifest}" "<${want}>" at)
      if(at EQUAL -1)
        string(APPEND errors "\n  manifest.ttl does not declare <${want}>")
      endif()
    elseif(key STREQUAL "ui_uri")
      string(FIND "${manifest}" "<${want}>" at)
      if(at EQUAL -1)
        string(APPEND errors "\n  manifest.ttl does not declare the UI <${want}>")
      endif()
    endif()
  else()
    string(APPEND errors "\n  compat.txt: unknown line '${line}'")
  endif()
endforeach()

if(nports EQUAL 0)
  string(APPEND errors "\n  compat.txt lists no ports")
endif()
if(errors)
  message(FATAL_ERROR "${NAME}: released identity changed; existing sessions would break:${errors}")
endif()
if(max_gen GREATER max_rec)
  math(EXPR added "${max_gen} - ${max_rec}")
  message(STATUS "${NAME}: ${added} LV2 port(s) added after the released ones")
endif()
message(STATUS "${NAME}: URI, IDs and ${nports} LV2 ports match compat.txt")
