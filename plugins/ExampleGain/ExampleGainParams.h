/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Parameter indices and ranges shared by the plugin and its UI. The order is the LV2
 * control-port order: append new parameters, never reorder. */
#ifndef EXAMPLE_GAIN_PARAMS_H
#define EXAMPLE_GAIN_PARAMS_H

enum {
    kParamGain = 0,
    kParamCount
};

#define kGainMin     (-60.0f)
#define kGainMax     12.0f
#define kGainDefault 0.0f

#endif
