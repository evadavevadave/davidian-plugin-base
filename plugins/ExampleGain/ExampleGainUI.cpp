/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Example Gain, UI side: the house-style panel described as data. Everything else
 * (drawing, mouse, host edits, every format) comes from common/ui/dpf_panel_ui.hpp. */
#include "ui/dpf_panel_ui.hpp"

#include "ExampleGainParams.h"

static const dui::Widget WIDGETS[] = {
    { dui::W_KNOB, kParamGain, 210, 165, 42, kGainMin, kGainMax, kGainDefault, "GAIN", "OUTPUT GAIN",
      "-60", "+12", {0}, 0, "%+.1f DB" },
};

static const dui::PanelSection SECTIONS[] = {
    { 16, 76, 404, 284, "OUTPUT" },
};

static const dui::PanelSpec SPEC = {
    DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT,
    "GAIN", "EXAMPLE PLUGIN", "EXAMPLE GAIN", "DRAG - DOUBLE-CLICK RESETS",
    { 280, 14, 404, 56 }, 260,
    SECTIONS, 1, WIDGETS, 1, nullptr, nullptr
};

START_NAMESPACE_DISTRHO

UI* createUI()
{
    return new DavidianPanelUI(SPEC);
}

END_NAMESPACE_DISTRHO
