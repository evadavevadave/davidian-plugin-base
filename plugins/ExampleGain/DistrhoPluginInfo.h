/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Example Gain: the smallest complete Davidian plugin, proving that one build makes LV2,
 * VST3, CLAP, AU and the standalone app. Copy this folder to start a new plugin.
 *
 * Identity: once released, never change the URIs or IDs below (compat.txt records them
 * and a test fails if they drift). For a port of an existing LV2 plugin, keep its URI. */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Davidian"
#define DISTRHO_PLUGIN_NAME    "Example Gain"
#define DISTRHO_PLUGIN_URI     "urn:davidian:example-gain"
#define DISTRHO_UI_URI         DISTRHO_PLUGIN_URI "#ui"
#define DISTRHO_PLUGIN_CLAP_ID "com.davidian.example-gain"

/* Four-character codes: AU manufacturer and subtype, also part of the VST3 class ID. */
#define DISTRHO_PLUGIN_BRAND_ID  Dvdn
#define DISTRHO_PLUGIN_UNIQUE_ID DxGn

#define DISTRHO_PLUGIN_HAS_UI      1
#define DISTRHO_PLUGIN_IS_RT_SAFE  1
#define DISTRHO_PLUGIN_NUM_INPUTS  2
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2

#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:AmplifierPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Tools|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "utility", "stereo"

/* House-style panel (common/ui/dpf_panel_ui.hpp): software-drawn, shown as an OpenGL
 * texture at a fixed size, scaled by the host's UI scale factor. */
#define DISTRHO_UI_USE_NANOVG      0
#define DISTRHO_UI_USER_RESIZABLE  0
#define DISTRHO_UI_DEFAULT_WIDTH   420
#define DISTRHO_UI_DEFAULT_HEIGHT  300

#endif
