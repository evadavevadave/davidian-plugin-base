/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Common-cathode triode gain stage using Koren's triode equation, solved against the
 * stage's AC load line into a lookup table when the stage is built.
 *
 *   [1] N. Koren, "Improved vacuum tube models for SPICE simulations", Glass Audio 8(5),
 *       1996. Triode equation and 12AX7 parameters.
 *
 * Building a stage (triode_build) solves the operating point and fills a 4096-entry
 * table: do it at instantiate / sample-rate change, never per block. Running a stage
 * (triode_run) is a table lookup plus two one-pole filters and is real-time safe.
 *
 * Also here: ShuntStage, a triode with plate-to-grid shunt feedback, solved per sample
 * with a safeguarded Newton iteration.
 *
 * Taken from Davidian Brownstone 100; the tube parameters are now a struct so other
 * types can be added. With KOREN_12AX7 the results are unchanged. */
#ifndef DAVIDIAN_TRIODE_H
#define DAVIDIAN_TRIODE_H

#include "filters.h"

#define TRIODE_TABLE_N 4096

/* ---------------------------------------------------- Koren triode model */

typedef struct {
    double mu, ex, kg1, kp, kvb;
} KorenTube;

static const KorenTube KOREN_12AX7 = { 100.0, 1.4, 1060.0, 600.0, 300.0 };

/* Plate current (A) for plate-cathode and grid-cathode voltages. */
static inline double koren_ip(const KorenTube* t, double vpk, double vgk)
{
    if (vpk <= 0.0) return 0.0;
    double a  = t->kp * (1.0 / t->mu + vgk / sqrt(t->kvb + vpk * vpk));
    double sp = a > 30.0 ? a : log1p(exp(a));
    double e1 = vpk / t->kp * sp;
    return e1 > 0.0 ? 2.0 * pow(e1, t->ex) / t->kg1 : 0.0;
}

static inline double koren_12ax7(double vpk, double vgk)
{
    return koren_ip(&KOREN_12AX7, vpk, vgk);
}

/* ------------------------------------------------------- operating point */

typedef struct {
    const KorenTube* tube;
    double B;     /* supply */
    double Ra;    /* DC plate resistor */
    double Rac;   /* AC load: Ra || next stage */
    double Rk;    /* cathode resistor */
    double ip0;   /* quiescent current */
    double vp0;   /* quiescent plate voltage (to ground) */
    double vk0;   /* quiescent cathode voltage */
} StageDC;

/* Plate current for grid voltage vin (to ground), on the AC load line.
 * rk_dyn: cathode resistance seen by the signal (0 when bypassed). */
static inline double solve_ip(const StageDC* d, double vin, double rk_dyn)
{
    double lo = 0.0, hi = d->ip0 + d->vp0 / d->Rac + 0.01;
    for (int i = 0; i < 64; ++i) {
        double ip = 0.5 * (lo + hi);
        double vk = rk_dyn > 0.0 ? ip * rk_dyn : d->vk0;
        double vp = d->vp0 - (ip - d->ip0) * d->Rac;
        double f  = ip - koren_ip(d->tube, vp - vk, vin - vk);
        if (f > 0.0) hi = ip; else lo = ip;
    }
    return 0.5 * (lo + hi);
}

static inline void solve_dc(StageDC* d)
{
    double lo = 0.0, hi = d->B / (d->Ra + d->Rk);
    for (int i = 0; i < 64; ++i) {
        double ip = 0.5 * (lo + hi);
        double vk = ip * d->Rk;
        double f  = ip - koren_ip(d->tube, d->B - ip * d->Ra - vk, -vk);
        if (f > 0.0) hi = ip; else lo = ip;
    }
    d->ip0 = 0.5 * (lo + hi);
    d->vk0 = d->ip0 * d->Rk;
    d->vp0 = d->B - d->ip0 * d->Ra;
}

/* ------------------------------------------------------------ gain stage */

typedef struct {
    float   table[TRIODE_TABLE_N]; /* AC plate voltage (inverting) vs grid voltage */
    double  lo, hi, inv_step;
    double  grid_thr;       /* grid voltage where grid conduction starts */
    double  grid_knee;      /* softness of grid-conduction limiting */
    int     bypassed;
    double  cath_g;         /* low-frequency gain ratio for a bypassed cathode */
    double  Rk;
    OnePole cath;           /* cathode bypass shelf */
    OnePole miller;         /* grid-stopper / Miller-capacitance low-pass */
    int     use_miller;
} Triode;

/* B: supply (V). Ra: plate resistor. Rnext: load of the next stage (use 1e12 when the
 * next network is simulated with this stage's source impedance). Rk: cathode resistor.
 * bypassed: cathode has a bypass capacitor (set it with triode_set_cathode_cap).
 * knee: softness of grid-conduction limiting (V). */
static inline void triode_build_tube(Triode* t, const KorenTube* tube, double B, double Ra,
                                     double Rnext, double Rk, int bypassed, double knee)
{
    StageDC d;
    d.tube = tube;
    d.B   = B;
    d.Ra  = Ra;
    d.Rk  = Rk;
    d.Rac = Ra * Rnext / (Ra + Rnext);
    solve_dc(&d);

    double rk_dyn = bypassed ? 0.0 : Rk;
    t->bypassed   = bypassed;
    t->Rk         = Rk;
    t->grid_knee  = knee;

    /* Grid-conduction threshold: vin where Vgk reaches 0. */
    if (bypassed) {
        t->grid_thr = d.vk0;
    } else {
        double lo = 0.0, hi = 400.0;
        for (int i = 0; i < 60; ++i) {
            double v = 0.5 * (lo + hi);
            if (v - solve_ip(&d, v, Rk) * Rk > 0.0) hi = v; else lo = v;
        }
        t->grid_thr = 0.5 * (lo + hi);
    }

    t->lo       = fmin(t->grid_thr, 0.0) - 60.0;
    t->hi       = t->grid_thr + 4.0 * knee + 0.5;
    t->inv_step = (TRIODE_TABLE_N - 1) / (t->hi - t->lo);

    for (int i = 0; i < TRIODE_TABLE_N; ++i) {
        double vin  = t->lo + i / t->inv_step;
        double ip   = solve_ip(&d, vin, rk_dyn);
        double vp   = d.vp0 - (ip - d.ip0) * d.Rac;
        t->table[i] = (float)(vp - d.vp0);
    }
    /* Remove the offset at the quiescent input (vin = 0). */
    {
        double off = d.vp0 - (solve_ip(&d, 0.0, rk_dyn) - d.ip0) * d.Rac - d.vp0;
        for (int i = 0; i < TRIODE_TABLE_N; ++i) t->table[i] -= (float)off;
    }

    /* Small-signal gain ratio unbypassed / bypassed, for the cathode shelf. */
    t->cath_g = 1.0;
    if (bypassed) {
        const double dv = 0.01;
        double gb = (solve_ip(&d, dv, 0.0) - solve_ip(&d, -dv, 0.0));
        double gu = (solve_ip(&d, dv, Rk) - solve_ip(&d, -dv, Rk));
        if (gb > 0.0) t->cath_g = gu / gb;
    }
    t->cath.s     = 0.0;
    t->miller.s   = 0.0;
    t->use_miller = 0;
}

/* A 12AX7 stage (Brownstone's triode_build). */
static inline void triode_build(Triode* t, double B, double Ra, double Rnext, double Rk,
                                int bypassed, double knee)
{
    triode_build_tube(t, &KOREN_12AX7, B, Ra, Rnext, Rk, bypassed, knee);
}

static inline void triode_set_cathode_cap(Triode* t, double Ck, double fs)
{
    if (!t->bypassed) return;
    /* zero at 1/(2 pi Rk Ck), pole at zero / g */
    double fz = 1.0 / (2.0 * M_PI * t->Rk * Ck);
    op_set(&t->cath, fz / t->cath_g, fs);
}

static inline void triode_set_miller(Triode* t, double fc, double fs)
{
    t->use_miller = fc > 0.0;
    if (t->use_miller) op_set(&t->miller, fc, fs);
}

static inline void triode_reset(Triode* t)
{
    t->cath.s = t->miller.s = 0.0;
}

/* Static transfer of a stage (grid limiting + table), with no filters: for solvers. */
static inline double triode_static(const Triode* t, double x)
{
    if (x > t->grid_thr) {
        double k = t->grid_knee;
        x = t->grid_thr + k * tanh((x - t->grid_thr) / k);
    }
    double pos = (x - t->lo) * t->inv_step;
    if (pos <= 0.0) return t->table[0];
    if (pos >= TRIODE_TABLE_N - 1) return t->table[TRIODE_TABLE_N - 1];
    int    i  = (int)pos;
    double fr = pos - i;
    return t->table[i] + fr * (t->table[i + 1] - t->table[i]);
}

/* One sample through the stage: Miller low-pass, cathode shelf, grid limiting, table. */
static inline double triode_run(Triode* t, double x)
{
    if (t->use_miller) x = op_lp(&t->miller, x);
    if (t->bypassed) x = shelf_low(&t->cath, t->cath_g, x);
    return triode_static(t, x);
}

/* ------------------------------------------- shunt-feedback gain stage */
/* r_in feeds the grid, r_gnd goes to ground and r_fb feeds the plate signal back to the
 * grid. The grid voltage solves
 *     vg = a vin + b T(vg),  a = G_in / G,  b = G_fb / G,  G = G_in + G_fb + G_gnd,
 * where T is the stage's (inverting) plate transfer. T is decreasing, so the residual is
 * strictly increasing and Newton converges from any start. */
typedef struct {
    Triode t;
    double a, b;
    double vg;
} ShuntStage;

static inline void shunt_setup(ShuntStage* s, double r_in, double r_fb, double r_gnd)
{
    double g_in = 1.0 / r_in, g_fb = 1.0 / r_fb, g = g_in + g_fb + 1.0 / r_gnd;
    s->a = g_in / g;
    s->b = g_fb / g;
    s->vg = 0.0;
}

/* Safeguarded Newton: Newton steps inside a bracket that always holds the root, with a
 * bisection whenever a step would leave it. Plain Newton can cycle on the clipped,
 * piecewise-linear transfer, which shows up as spurious low tones under heavy drive. */
static inline double shunt_run(ShuntStage* s, double vin)
{
    const double h = 0.01;
    /* |T| is bounded by the table, so the root lies within this bracket. */
    const double tmax = fmax(fabs(s->t.table[0]), fabs(s->t.table[TRIODE_TABLE_N - 1]));
    double lo = s->a * vin - s->b * tmax - 1.0, hi = s->a * vin + s->b * tmax + 1.0;
    double vg = clampd(s->vg, lo, hi);
    for (int it = 0; it < 40; ++it) {
        double f = vg - s->b * triode_static(&s->t, vg) - s->a * vin;
        if (f > 0.0) hi = vg; else lo = vg;
        if (fabs(f) < 1e-7 || hi - lo < 1e-7) break;
        double dt = (triode_static(&s->t, vg + h) - triode_static(&s->t, vg - h)) / (2.0 * h);
        double next = vg - f / (1.0 - s->b * dt);
        vg = (next > lo && next < hi) ? next : 0.5 * (lo + hi);
    }
    s->vg = vg;
    return triode_static(&s->t, vg);
}

#endif
