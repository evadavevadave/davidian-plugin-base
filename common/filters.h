/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Basic linear filters: a topology-preserving one-pole and a DF2T biquad.
 *
 *   [3] R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients"
 *       (Audio EQ Cookbook). Low-pass, high-pass and peaking biquads (bq_*).
 *   [4] V. Zavalishin, "The Art of VA Filter Design", Native Instruments, 2012-2020.
 *       Topology-preserving (trapezoidal) one-pole filters (OnePole).
 *
 * Taken unchanged from Davidian Brownstone 100. */
#ifndef DAVIDIAN_FILTERS_H
#define DAVIDIAN_FILTERS_H

#include "dsp_util.h"

/* ---------------------------------------------------- TPT one-pole filter */

typedef struct {
    double G; /* g / (1 + g) */
    double s;
} OnePole;

static inline void op_set(OnePole* f, double fc, double fs)
{
    double g = tan(M_PI * clampd(fc, 1.0, 0.45 * fs) / fs);
    f->G = g / (1.0 + g);
}

static inline double op_lp(OnePole* f, double x)
{
    double v = (x - f->s) * f->G;
    double y = v + f->s;
    f->s     = y + v;
    return y;
}

static inline double op_hp(OnePole* f, double x)
{
    return x - op_lp(f, x);
}

/* Low shelf: gain g at DC, unity at HF, pole (LP cutoff) set in f. */
static inline double shelf_low(OnePole* f, double g, double x)
{
    return x + (g - 1.0) * op_lp(f, x);
}

/* ------------------------------------------------------------ biquad DF2T */

typedef struct {
    double b0, b1, b2, a1, a2;
    double z1, z2;
} Biquad;

static inline double bq_run(Biquad* q, double x)
{
    double y = q->b0 * x + q->z1;
    q->z1    = q->b1 * x - q->a1 * y + q->z2;
    q->z2    = q->b2 * x - q->a2 * y;
    return y;
}

static inline void bq_reset(Biquad* q) { q->z1 = q->z2 = 0.0; }

static inline void bq_identity(Biquad* q)
{
    q->b0 = 1.0;
    q->b1 = q->b2 = q->a1 = q->a2 = 0.0;
}

static inline void bq_lowpass(Biquad* q, double fc, double Q, double fs)
{
    double w = 2.0 * M_PI * fc / fs, c = cos(w), a = sin(w) / (2.0 * Q);
    double a0 = 1.0 + a;
    q->b0 = (1.0 - c) / 2.0 / a0;
    q->b1 = (1.0 - c) / a0;
    q->b2 = q->b0;
    q->a1 = -2.0 * c / a0;
    q->a2 = (1.0 - a) / a0;
}

static inline void bq_highpass(Biquad* q, double fc, double Q, double fs)
{
    double w = 2.0 * M_PI * fc / fs, c = cos(w), a = sin(w) / (2.0 * Q);
    double a0 = 1.0 + a;
    q->b0 = (1.0 + c) / 2.0 / a0;
    q->b1 = -(1.0 + c) / a0;
    q->b2 = q->b0;
    q->a1 = -2.0 * c / a0;
    q->a2 = (1.0 - a) / a0;
}

static inline void bq_peak(Biquad* q, double fc, double Q, double gain_db, double fs)
{
    double A = pow(10.0, gain_db / 40.0);
    double w = 2.0 * M_PI * fc / fs, c = cos(w), a = sin(w) / (2.0 * Q);
    double a0 = 1.0 + a / A;
    q->b0 = (1.0 + a * A) / a0;
    q->b1 = -2.0 * c / a0;
    q->b2 = (1.0 - a * A) / a0;
    q->a1 = -2.0 * c / a0;
    q->a2 = (1.0 - a / A) / a0;
}

/* Magnitude of one biquad at normalized angular frequency w (radians per sample). */
static inline double bq_mag(const Biquad* q, double w)
{
    double c1 = cos(w), s1 = sin(w), c2 = cos(2 * w), s2 = sin(2 * w);
    double nr = q->b0 + q->b1 * c1 + q->b2 * c2, ni = -(q->b1 * s1 + q->b2 * s2);
    double dr = 1.0 + q->a1 * c1 + q->a2 * c2, di = -(q->a1 * s1 + q->a2 * s2);
    return sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}

/* Butterworth low-pass of order 2 * n as n cascaded biquads (pole-pair Qs). */
static inline void bq_butter_lowpass(Biquad* f, int n, double fc, double fs)
{
    for (int k = 0; k < n; ++k) {
        double Q = 1.0 / (2.0 * sin((2.0 * (k + 1) - 1.0) * M_PI / (4.0 * n)));
        bq_lowpass(&f[k], fc, Q, fs);
        f[k].z1 = f[k].z2 = 0.0;
    }
}

#endif
