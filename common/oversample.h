/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Integer oversampling for nonlinear stages: zero-stuffing up, a Butterworth anti-image
 * filter, the nonlinear code at the high rate, a Butterworth anti-alias filter, then
 * decimation. With factor 4 and 6 stages this is exactly Brownstone 100's oversampler.
 *
 *     Oversampler os;
 *     os_init(&os, 4, fs);                      (not on the audio thread's hot path)
 *     ...
 *     double buf[OS_MAX_FACTOR];
 *     os_up(&os, x, buf);
 *     for (int i = 0; i < os.factor; ++i) buf[i] = nonlinear(buf[i]);
 *     y = os_down(&os, buf);
 *
 * Only the nonlinear stages should run inside; linear networks belong at the base rate.
 * The filters are minimum-phase IIR, so the added latency is a fraction of a sample at
 * the base rate and is reported to hosts as zero. Factor 1 bypasses both filters.
 * Everything is in fixed arrays: no allocation, and os_init is safe to call when the
 * factor or sample rate changes (it clears the filter state). */
#ifndef DAVIDIAN_OVERSAMPLE_H
#define DAVIDIAN_OVERSAMPLE_H

#include "filters.h"

#define OS_MAX_FACTOR 16
#define OS_MAX_STAGES 8
#define OS_DEFAULT_STAGES 6 /* 12th-order Butterworth, as in Brownstone */

typedef struct {
    int    factor;     /* 1, 2, 4, 8 or 16 */
    int    stages;     /* biquads per filter */
    double fs;         /* base rate */
    double fs_os;      /* fs * factor */
    Biquad up[OS_MAX_STAGES], down[OS_MAX_STAGES];
} Oversampler;

/* Valid factors are 1, 2, 4, 8 and 16; anything else is rounded down to one of them. */
static inline int os_valid_factor(int f)
{
    int v = 1;
    while (v * 2 <= f && v < OS_MAX_FACTOR) v *= 2;
    return v;
}

/* Full control over the filter order. The cutoff is 0.45 * fs, capped at 20 kHz. */
static inline void os_init_stages(Oversampler* o, int factor, int stages, double fs)
{
    const double fc = fmin(0.45 * fs, 20000.0);
    o->factor = os_valid_factor(factor);
    o->stages = stages < 1 ? 1 : (stages > OS_MAX_STAGES ? OS_MAX_STAGES : stages);
    o->fs = fs;
    o->fs_os = fs * o->factor;
    bq_butter_lowpass(o->up, o->stages, fc, o->fs_os);
    bq_butter_lowpass(o->down, o->stages, fc, o->fs_os);
}

static inline void os_init(Oversampler* o, int factor, double fs)
{
    os_init_stages(o, factor, OS_DEFAULT_STAGES, fs);
}

static inline void os_reset(Oversampler* o)
{
    for (int k = 0; k < o->stages; ++k) {
        bq_reset(&o->up[k]);
        bq_reset(&o->down[k]);
    }
}

/* One base-rate sample in, `factor` high-rate samples out. */
static inline void os_up(Oversampler* o, double x, double* out)
{
    if (o->factor == 1) {
        out[0] = x;
        return;
    }
    for (int i = 0; i < o->factor; ++i) {
        double u = (i == 0) ? x * o->factor : 0.0;
        for (int q = 0; q < o->stages; ++q) u = bq_run(&o->up[q], u);
        out[i] = u;
    }
}

/* `factor` high-rate samples in, one base-rate sample out. */
static inline double os_down(Oversampler* o, const double* in)
{
    double y = 0.0;
    if (o->factor == 1)
        return in[0];
    for (int i = 0; i < o->factor; ++i) {
        y = in[i];
        for (int q = 0; q < o->stages; ++q) y = bq_run(&o->down[q], y);
    }
    return y; /* keep the last of each group */
}

#endif
