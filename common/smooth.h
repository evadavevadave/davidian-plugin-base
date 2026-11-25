/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Parameter smoothing, so no control change reaches the audio as a step (a click).
 *
 * Two forms:
 *   Glide     Brownstone's control-block glide: the value moves a fixed fraction of the way
 *             to its target once per block of samples, and reports whether it moved, so
 *             expensive recalculation (a circuit network, a filter design) happens only
 *             while a control is actually moving.
 *   Smoother  a per-sample one-pole, for gains and other cheap values that can be
 *             applied every sample.
 *
 * Both start at the target the first time they are set after a reset (no glide from zero
 * when a plugin is activated), and both are allocation-free and real-time safe. */
#ifndef DAVIDIAN_SMOOTH_H
#define DAVIDIAN_SMOOTH_H

#include "dsp_util.h"

/* Moves v toward target by the fraction k; snaps once within 1e-6. Returns 1 if v
 * changed. This is Brownstone's glide(), unchanged. */
static inline int glide(double* v, double target, double k)
{
    double d = target - *v;
    if (fabs(d) < 1e-6) {
        if (*v != target) { *v = target; return 1; }
        return 0;
    }
    *v += d * k;
    return 1;
}

/* Glide fraction for one step of `block` samples with time constant `tau` seconds. */
static inline double glide_coef(double tau, double block, double fs)
{
    return tau > 0.0 ? 1.0 - exp(-block / (tau * fs)) : 1.0;
}

/* ---------------------------------------------------------- block glide */

typedef struct {
    double value;
    double k;      /* fraction per step */
    int    primed; /* 0 until the first target after a reset */
} Glide;

static inline void glide_init(Glide* g, double tau, double block, double fs)
{
    g->k = glide_coef(tau, block, fs);
    g->value = 0.0;
    g->primed = 0;
}

static inline void glide_reset(Glide* g) { g->primed = 0; }

/* Jump straight to v (after a reset, or when a switch selects another control). */
static inline void glide_snap(Glide* g, double v)
{
    g->value = v;
    g->primed = 1;
}

/* One control-block step toward target. Returns 1 if the value changed. */
static inline int glide_step(Glide* g, double target)
{
    if (!g->primed) {
        glide_snap(g, target);
        return 1;
    }
    return glide(&g->value, target, g->k);
}

/* ------------------------------------------------------ per-sample smoother */

typedef struct {
    double value, target;
    double k;
    int    primed;
} Smoother;

static inline void smoother_init(Smoother* s, double tau, double fs)
{
    s->k = glide_coef(tau, 1.0, fs);
    s->value = s->target = 0.0;
    s->primed = 0;
}

static inline void smoother_reset(Smoother* s) { s->primed = 0; }

static inline void smoother_set(Smoother* s, double target)
{
    s->target = target;
    if (!s->primed) {
        s->value = target;
        s->primed = 1;
    }
}

static inline double smoother_next(Smoother* s)
{
    double d = s->target - s->value;
    s->value = fabs(d) < 1e-9 ? s->target : s->value + d * s->k;
    return s->value;
}

static inline int smoother_settled(const Smoother* s) { return s->value == s->target; }

#endif
