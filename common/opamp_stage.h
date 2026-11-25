/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Non-inverting op-amp gain stage (the classic distortion-pedal 741 stage):
 *
 *   H(s) = 1 + Zf/Zg
 *   Zf = Rf || Cf          Zg = Rg + 1/(s Cg)
 *
 * bilinear-transformed, followed by the op-amp's finite gain-bandwidth (a one-pole
 * low-pass whose corner falls as the gain rises), its slew-rate limit, and a soft limit
 * for the output swing.
 *
 *     OpAmpStage s;
 *     opamp_stage_init(&s, fs_os);                       (CANARY parts, 741 model)
 *     opamp_stage_set_rg(&s, 4.7e3 + pot_ohms);         (gain control; per block)
 *     y = opamp_stage_process(&s, x);                    (per sample)
 *
 * Taken from Davidian Canary Plus (gain_stage.h). The part values and op-amp figures are
 * now structs; OPAMP_PARTS_DEFAULT and OPAMP_741 hold Canary's (results unchanged). */
#ifndef DAVIDIAN_OPAMP_STAGE_H
#define DAVIDIAN_OPAMP_STAGE_H

#include "dsp_util.h"

typedef struct {
    double rf;   /* feedback resistor */
    double cf;   /* feedback capacitor */
    double cg;   /* capacitor in the ground leg */
    double r0;   /* fixed resistor in the ground leg */
    double pot;  /* gain pot in the ground leg (full value) */
} OpAmpStageParts;

/* 1M || 10pF feedback, 4.7k + 1M pot + 0.047uF to ground (Canary Plus). */
static const OpAmpStageParts OPAMP_PARTS_DEFAULT = { 1e6, 10e-12, 47e-9, 4.7e3, 1e6 };

typedef struct {
    double gbw_hz;        /* gain-bandwidth product */
    double slew_v_per_s;  /* slew rate */
    double vsat;          /* output swing limit */
    double vknee;         /* where the soft limit starts */
} OpAmpModel;

/* Not from a schematic: typical 741 figures on a 9 V single supply. */
static const OpAmpModel OPAMP_741 = { 1e6, 0.5e6, 3.0, 2.5 };

typedef struct {
    OpAmpStageParts parts;
    OpAmpModel amp;
    double b0, b1, b2, a1, a2, z1, z2;
    double lp_g, lp_z;
    double slew_step;
    double fs;
} OpAmpStage;

/* Gain-bandwidth corner for ground-leg resistance rg. */
static inline double opamp_gbw_corner_hz(const OpAmpStage* s, double rg)
{
    return s->amp.gbw_hz / (1.0 + s->parts.rf / rg);
}

/* Sets the ground-leg resistance (the gain control). Real-time safe. */
static inline void opamp_stage_set_rg(OpAmpStage* s, double rg)
{
    const double a = s->parts.rf * s->parts.cf;
    const double b = rg * s->parts.cg;
    const double k = 2.0 * s->fs;
    const double k2 = k * k;
    const double d1 = a + b;
    const double d2 = a * b;
    const double n1 = a + b + s->parts.rf * s->parts.cg;
    const double n2 = a * b;
    const double a0 = 1.0 + d1 * k + d2 * k2;
    double fc, w;
    s->b0 = (1.0 + n1 * k + n2 * k2) / a0;
    s->b1 = (2.0 - 2.0 * n2 * k2) / a0;
    s->b2 = (1.0 - n1 * k + n2 * k2) / a0;
    s->a1 = (2.0 - 2.0 * d2 * k2) / a0;
    s->a2 = (1.0 - d1 * k + d2 * k2) / a0;
    fc = opamp_gbw_corner_hz(s, rg);
    if (fc > 0.4 * s->fs)
        fc = 0.4 * s->fs;
    w = tan(M_PI * fc / s->fs);
    s->lp_g = w / (1.0 + w);
}

static inline void opamp_stage_init_parts(OpAmpStage* s, double fs, const OpAmpStageParts* parts,
                                          const OpAmpModel* amp)
{
    s->parts = *parts;
    s->amp = *amp;
    s->fs = fs;
    s->z1 = s->z2 = s->lp_z = 0.0;
    s->slew_step = s->amp.slew_v_per_s / fs;
    opamp_stage_set_rg(s, s->parts.r0 + s->parts.pot);
}

static inline void opamp_stage_init(OpAmpStage* s, double fs)
{
    opamp_stage_init_parts(s, fs, &OPAMP_PARTS_DEFAULT, &OPAMP_741);
}

/* Linear up to the knee, then eases into vsat with a continuous slope. */
static inline double opamp_limit(const OpAmpModel* m, double x)
{
    const double span = m->vsat - m->vknee;
    double ax = fabs(x);
    double y;
    if (ax <= m->vknee)
        return x;
    y = m->vknee + span * tanh((ax - m->vknee) / span);
    return x < 0.0 ? -y : y;
}

static inline double opamp_stage_process(OpAmpStage* s, double x)
{
    double y = s->b0 * x + s->z1;
    double v;
    s->z1 = s->b1 * x - s->a1 * y + s->z2;
    s->z2 = s->b2 * x - s->a2 * y;
    /* One integrator models both limits, as in the real part: small signals see the
     * gain-bandwidth low-pass, and the tanh caps how fast it can move. A hard slew clamp
     * here aliases badly. The integrator advances by about 2v per sample, hence the
     * half step. */
    v = (y - s->lp_z) * s->lp_g;
    {
        const double cap = 0.5 * s->slew_step;
        if (fabs(v) > 0.05 * cap)
            v = cap * tanh(v / cap);
    }
    y = v + s->lp_z;
    s->lp_z = y + v;
    return opamp_limit(&s->amp, y);
}

#endif
