/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Small helpers shared by every DSP header: constants, clamping, decibels and pot tapers.
 *
 * All of common/ (except common/ui/) is plain C99 that also compiles as C++, with no
 * plugin-format, GUI or OS headers, so the same code runs in every format and in the
 * offline test harness. */
#ifndef DAVIDIAN_DSP_UTIL_H
#define DAVIDIAN_DSP_UTIL_H

#include <math.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

static inline double clampd(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline double db_to_gain(double db) { return pow(10.0, db / 20.0); }
static inline double gain_to_db(double g)  { return 20.0 * log10(g + 1e-30); }

/* Audio ("A") taper pot: 10% resistance at mid travel. */
static inline double taper_audio(double p)
{
    p = clampd(p, 0.0, 1.0);
    return (pow(81.0, p) - 1.0) / 80.0;
}

/* Steeper audio taper ("A3" pre-gain pots), about 3% at mid travel. */
static inline double taper_pre(double p)
{
    p = clampd(p, 0.0, 1.0);
    return (pow(1045.0, p) - 1.0) / 1044.0;
}

#endif
