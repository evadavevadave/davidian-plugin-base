/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Linear-phase FIR oversampling: an interpolator and a decimator sharing one
 * Kaiser-windowed sinc. The prototype has 32 * factor + 1 taps, so each filter delays by
 * 16 base-rate samples and the pair adds exactly FIR_OS_LATENCY (32) samples at any
 * factor: report that to the host, and delay any dry path by the same amount.
 *
 *     FirOversampler os;
 *     fir_os_init(&os, 8);                     (allocates: instantiate only)
 *     fir_os_push_input(&os, x);
 *     for (phase = 0; phase < os.factor; ++phase) {
 *         y = nonlinear(fir_os_interpolate(&os, phase));
 *         fir_os_push_output(&os, y);
 *         if (phase == 0) out = fir_os_decimate(&os);   (whole-sample total delay)
 *     }
 *
 * Linear phase keeps the waveform shape of the oversampled stage; common/oversample.h is
 * the minimum-phase (IIR, near-zero latency) alternative.
 * Taken unchanged from Davidian Canary Plus (oversampler.h). */
#ifndef DAVIDIAN_FIR_OVERSAMPLE_H
#define DAVIDIAN_FIR_OVERSAMPLE_H

#include <stdlib.h>
#include <string.h>

#include "dsp_util.h"

#define FIR_OS_LATENCY 32
#define FIR_OS_UP_LEN 33
#define FIR_OS_KAISER_BETA 9.0

typedef struct {
    int factor;
    int n_taps;
    double* taps;  /* factor * FIR_OS_UP_LEN entries, zero padded */
    double up[FIR_OS_UP_LEN * 2];
    int up_pos;
    double* down;  /* n_taps * 2 entries, mirrored for contiguous reads */
    int down_pos;
} FirOversampler;

static inline double fir_os_bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    int k;
    for (k = 1; k < 64; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-14 * sum)
            break;
    }
    return sum;
}

/* Allocates the filters (not real-time safe). Returns 0 on allocation failure; call
 * fir_os_free() either way. */
static inline int fir_os_init(FirOversampler* o, int factor)
{
    const int n_taps = 32 * factor + 1;
    const double centre = 0.5 * (n_taps - 1);
    const double fc = 0.5 / factor; /* base-rate Nyquist, cycles per sample */
    double sum = 0.0;
    int n;

    memset(o, 0, sizeof(*o));
    o->factor = factor;
    o->n_taps = n_taps;
    o->taps = (double*)calloc((size_t)factor * FIR_OS_UP_LEN, sizeof(double));
    o->down = (double*)calloc((size_t)n_taps * 2, sizeof(double));
    if (!o->taps || !o->down)
        return 0;

    for (n = 0; n < n_taps; n++) {
        double m = n - centre;
        double r = m / centre;
        double sinc = (m == 0.0) ? 2.0 * fc : sin(2.0 * M_PI * fc * m) / (M_PI * m);
        double win = fir_os_bessel_i0(FIR_OS_KAISER_BETA * sqrt(1.0 - r * r)) /
                     fir_os_bessel_i0(FIR_OS_KAISER_BETA);
        o->taps[n] = sinc * win;
        sum += o->taps[n];
    }
    for (n = 0; n < n_taps; n++)
        o->taps[n] /= sum;
    return 1;
}

static inline void fir_os_free(FirOversampler* o)
{
    free(o->taps);
    free(o->down);
    o->taps = o->down = NULL;
}

static inline void fir_os_reset(FirOversampler* o)
{
    memset(o->up, 0, sizeof(o->up));
    memset(o->down, 0, (size_t)o->n_taps * 2 * sizeof(double));
    o->up_pos = 0;
    o->down_pos = 0;
}

static inline void fir_os_push_input(FirOversampler* o, double x)
{
    o->up_pos = (o->up_pos == 0) ? FIR_OS_UP_LEN - 1 : o->up_pos - 1;
    o->up[o->up_pos] = x;
    o->up[o->up_pos + FIR_OS_UP_LEN] = x;
}

/* Interpolated sample for one phase of the newest input. */
static inline double fir_os_interpolate(const FirOversampler* o, int phase)
{
    const double* x = o->up + o->up_pos;
    const double* h = o->taps + phase;
    const int step = o->factor;
    double y = 0.0;
    int k;
    for (k = 0; k < FIR_OS_UP_LEN; k++)
        y += h[k * step] * x[k];
    return y * step;
}

static inline void fir_os_push_output(FirOversampler* o, double x)
{
    o->down_pos = (o->down_pos == 0) ? o->n_taps - 1 : o->down_pos - 1;
    o->down[o->down_pos] = x;
    o->down[o->down_pos + o->n_taps] = x;
}

static inline double fir_os_decimate(const FirOversampler* o)
{
    const double* x = o->down + o->down_pos;
    double y = 0.0;
    int k;
    for (k = 0; k < o->n_taps; k++)
        y += o->taps[k] * x[k];
    return y;
}

#endif
