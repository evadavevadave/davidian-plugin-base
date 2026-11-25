/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Unit tests for the shared DSP in common/: each module against an analytic or
 * physically expected result, then a small chain run at every supported sample rate and
 * a range of block sizes checking for NaN, infinity, denormals and DC. */
#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "davidian_dsp.h"
#include "check.h"

static const double RATES[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
#define N_RATES ((int)(sizeof RATES / sizeof RATES[0]))

/* Level of frequency f in x (Goertzel), as an amplitude. */
static double tone_amp(const double* x, int n, double f, double fs)
{
    const double w = 2.0 * M_PI * f / fs, c = 2.0 * cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        /* Hann window to keep leakage from the strong harmonics out of the alias bins. */
        double h = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1));
        double s0 = x[i] * h + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (0.5 * n);
}

static int is_denormal(double x)
{
    return x != 0.0 && fabs(x) < DBL_MIN;
}

/* ------------------------------------------------------------- dsp_util */

static void test_util(void)
{
    CHECK(clampd(5, 0, 1) == 1 && clampd(-5, 0, 1) == 0 && clampd(0.3, 0, 1) == 0.3, "clampd");
    CHECK(fabs(db_to_gain(-6.0206) - 0.5) < 1e-4, "db_to_gain");
    CHECK(fabs(gain_to_db(2.0) - 6.0206) < 1e-3, "gain_to_db");
    CHECK(taper_audio(0) == 0 && fabs(taper_audio(1) - 1) < 1e-12, "audio taper ends");
    CHECK(fabs(taper_audio(0.5) - 0.1) < 1e-12, "audio taper mid = 10%%, got %g", taper_audio(0.5));
    CHECK(fabs(taper_pre(0.5) - 0.03) < 0.002, "pre taper mid ~3%%, got %g", taper_pre(0.5));
}

/* -------------------------------------------------------------- filters */

static void test_filters(void)
{
    const double fs = 48000.0;
    Biquad q;
    OnePole op = { 0, 0 };

    bq_lowpass(&q, 1000.0, 0.70710678118654752, fs);
    CHECK(fabs(gain_to_db(bq_mag(&q, 2 * M_PI * 1000 / fs)) + 3.0103) < 0.01, "LP -3 dB at fc");
    CHECK(fabs(bq_mag(&q, 1e-6) - 1.0) < 1e-6, "LP unity at DC");
    bq_highpass(&q, 1000.0, 0.70710678118654752, fs);
    CHECK(fabs(gain_to_db(bq_mag(&q, 2 * M_PI * 1000 / fs)) + 3.0103) < 0.01, "HP -3 dB at fc");
    bq_peak(&q, 2000.0, 1.0, 6.0, fs);
    CHECK(fabs(gain_to_db(bq_mag(&q, 2 * M_PI * 2000 / fs)) - 6.0) < 0.01, "peak +6 dB at fc");

    /* Butterworth cascade: -3 dB at fc for any order, steep above. */
    {
        Biquad f[6];
        double m = 1.0, m2 = 1.0;
        bq_butter_lowpass(f, 6, 1000.0, fs);
        for (int k = 0; k < 6; ++k) {
            m *= bq_mag(&f[k], 2 * M_PI * 1000 / fs);
            m2 *= bq_mag(&f[k], 2 * M_PI * 2000 / fs);
        }
        CHECK(fabs(gain_to_db(m) + 3.0103) < 0.02, "butterworth -3 dB at fc, got %g", gain_to_db(m));
        CHECK(gain_to_db(m2) < -70.0, "12th order: below -70 dB an octave up, got %g", gain_to_db(m2));
    }

    /* TPT one-pole: unity DC gain, matched -3 dB point (bilinear, prewarped). */
    op_set(&op, 1000.0, fs);
    {
        double y = 0.0;
        for (int i = 0; i < 48000; ++i) y = op_lp(&op, 1.0);
        CHECK(fabs(y - 1.0) < 1e-9, "one-pole LP settles to 1, got %.12f", y);
    }
}

/* --------------------------------------------------------------- smooth */

static void test_smooth(void)
{
    const double fs = 48000.0;
    Glide g;
    Smoother s;

    glide_init(&g, 0.015, 32, fs);
    CHECK(glide_step(&g, 0.7) == 1 && g.value == 0.7, "first target snaps (no glide from 0)");
    CHECK(glide_step(&g, 0.7) == 0, "settled glide reports no change");
    {
        int steps = 0;
        while (glide_step(&g, 0.2) && steps < 100000) ++steps;
        CHECK(g.value == 0.2, "glide lands exactly on target");
        /* tau 15 ms at 32-sample blocks: ~15 blocks per tau; 1e-6 of 0.5 needs ~13 tau */
        CHECK(steps > 150 && steps < 300, "glide takes ~13 tau, took %d blocks", steps);
    }

    smoother_init(&s, 0.010, fs);
    smoother_set(&s, 1.0);
    CHECK(s.value == 1.0, "smoother starts at its first target");
    smoother_set(&s, 0.0);
    {
        double v = 1.0, maxstep = 0.0;
        int n = (int)(0.010 * fs);
        for (int i = 0; i < n; ++i) {
            double nv = smoother_next(&s);
            maxstep = fmax(maxstep, fabs(nv - v));
            v = nv;
        }
        CHECK(fabs(v - exp(-1.0)) < 0.01, "one tau reaches 1/e, got %g", v);
        CHECK(maxstep < 0.003, "no step larger than k (no click), max %g", maxstep);
        for (int i = 0; i < 48000; ++i) smoother_next(&s);
        CHECK(smoother_settled(&s), "smoother settles exactly");
    }
    smoother_reset(&s);
    smoother_set(&s, 0.5);
    CHECK(s.value == 0.5, "reset makes the next target immediate");
}

/* ------------------------------------------------------------- denormal */

static void test_denormal(void)
{
    volatile double tiny = DBL_MIN; /* smallest normal */
    volatile double half;
    DenormalGuard g;

    CHECK(dsp_bad(NAN) && dsp_bad(INFINITY) && dsp_bad(-INFINITY), "dsp_bad flags NaN/inf");
    CHECK(!dsp_bad(0.0) && !dsp_bad(1e300) && !dsp_bad(-1e-300), "dsp_bad passes finite");
    CHECK(undenormal(1e-35) == 0.0 && undenormal(1e-20) == 1e-20, "undenormal");

    half = tiny * 0.5;
    CHECK(is_denormal(half), "without the guard, DBL_MIN/2 is denormal");
    denormal_enter(&g);
#if defined(DAVIDIAN_DENORMAL_SSE) || defined(DAVIDIAN_DENORMAL_ARM64)
    CHECK(denormal_ftz_active(), "guard turns on flush-to-zero");
    half = tiny * 0.5;
    CHECK(half == 0.0, "under the guard DBL_MIN/2 flushes to 0, got %g", (double)half);
#else
    printf("note: no hardware flush-to-zero on this target\n");
#endif
    denormal_leave(&g);
    CHECK(!denormal_ftz_active(), "guard restores the previous mode");
}

/* ----------------------------------------------------------- oversample */

static void test_oversample(void)
{
    CHECK(os_valid_factor(0) == 1 && os_valid_factor(1) == 1 && os_valid_factor(3) == 2 &&
          os_valid_factor(4) == 4 && os_valid_factor(16) == 16 && os_valid_factor(64) == 16,
          "factor rounding");

    /* Passband: a linear "nonlinearity" leaves 1 kHz at unity gain for every factor. */
    for (int f = 1; f <= OS_MAX_FACTOR; f *= 2) {
        for (int r = 0; r < N_RATES; ++r) {
            const double fs = RATES[r];
            const int n = (int)fs / 4;
            Oversampler os;
            double* y = (double*)malloc(sizeof(double) * n);
            double buf[OS_MAX_FACTOR];
            os_init(&os, f, fs);
            for (int i = 0; i < n; ++i) {
                os_up(&os, sin(2 * M_PI * 1000.0 * i / fs), buf);
                y[i] = os_down(&os, buf);
            }
            {
                double a = tone_amp(y + n / 2, n / 2, 1000.0, fs);
                CHECK(fabs(gain_to_db(a)) < 0.1, "%dx at %.0f Hz: 1 kHz gain %+.3f dB", f, fs, gain_to_db(a));
            }
            free(y);
        }
    }

    /* Aliasing: hard-clip a 7 kHz sine at 44.1 kHz. The 5th harmonic (35 kHz) folds to
     * 9.1 kHz; oversampling must push that well down. */
    {
        const double fs = 44100.0;
        const int n = 32768;
        double alias_db[5];
        int idx = 0;
        double* y = (double*)malloc(sizeof(double) * n);
        for (int f = 1; f <= OS_MAX_FACTOR; f *= 2, ++idx) {
            Oversampler os;
            double buf[OS_MAX_FACTOR];
            os_init(&os, f, fs);
            for (int i = 0; i < n + 4096; ++i) {
                os_up(&os, 2.0 * sin(2 * M_PI * 7000.0 * i / fs), buf);
                for (int k = 0; k < f; ++k) buf[k] = clampd(buf[k], -1.0, 1.0);
                double v = os_down(&os, buf);
                if (i >= 4096) y[i - 4096] = v;
            }
            alias_db[idx] = gain_to_db(tone_amp(y, n, fs - 5 * 7000.0, fs) + 1e-12);
            printf("  hard clip 7 kHz, %2dx: alias at 9.1 kHz %6.1f dB\n", f, alias_db[idx]);
        }
        /* At 4x and up, what remains in this bin is window leakage and far-folded high
         * harmonics, so the levels are compared with a ceiling, not with each other. */
        CHECK(alias_db[0] > -40.0, "1x aliases audibly (sanity), %g dB", alias_db[0]);
        for (int k = 2; k < 5; ++k)
            CHECK(alias_db[k] < -70.0, "%dx alias below -70 dB, got %g", 1 << k, alias_db[k]);
        free(y);
    }
}

/* --------------------------------------------------------------- triode */

static void test_triode(void)
{
    static Triode t; /* 16 KB table: keep off the stack */
    static ShuntStage sh;
    const double fs = 192000.0;

    /* Koren 12AX7 near a typical operating point: about 1 mA at 250 V, -1.5 V. */
    {
        double ip = koren_12ax7(250.0, -1.5);
        CHECK(ip > 0.5e-3 && ip < 2e-3, "12AX7 Ip(250 V, -1.5 V) = %.3f mA", ip * 1e3);
        CHECK(koren_12ax7(250.0, -5.0) < 0.1e-3, "near cutoff at -5 V: %.4f mA", koren_12ax7(250.0, -5.0) * 1e3);
        CHECK(koren_12ax7(-1.0, 0.0) == 0.0, "no current with negative plate voltage");
    }

    /* Classic stage: 300 V, 100k plate, 1.5k bypassed cathode, 1M load. */
    triode_build(&t, 300.0, 100e3, 1e6, 1.5e3, 1, 0.4);
    {
        int mono = 1;
        for (int i = 1; i < TRIODE_TABLE_N; ++i)
            if (t.table[i] > t.table[i - 1] + 1e-4f) mono = 0;
        CHECK(mono, "plate transfer is non-increasing (inverting)");
        CHECK(fabs(triode_static(&t, 0.0)) < 0.05, "no offset at rest: %g", triode_static(&t, 0.0));
        double gain = (triode_static(&t, 0.01) - triode_static(&t, -0.01)) / 0.02;
        CHECK(gain < -45.0 && gain > -75.0, "bypassed small-signal gain %.1f (expect ~-60)", gain);
        CHECK(t.grid_thr > 0.8 && t.grid_thr < 2.5, "grid conduction starts at Vk0 = %.2f V", t.grid_thr);
        CHECK(t.cath_g > 0.3 && t.cath_g < 0.9, "unbypassed/bypassed gain ratio %.2f", t.cath_g);
    }
    triode_set_cathode_cap(&t, 22e-6, fs);
    triode_set_miller(&t, 20000.0, fs);
    {
        int bad = 0;
        double peak = 0.0;
        for (int i = 0; i < 20000; ++i) {
            double y = triode_run(&t, 200.0 * sin(2 * M_PI * 110.0 * i / fs)); /* absurd drive */
            bad |= dsp_bad(y);
            peak = fmax(peak, fabs(y));
        }
        CHECK(!bad && peak < 400.0, "bounded under extreme drive, peak %.1f V", peak);
    }

    /* Shunt-feedback stage with Brownstone's V5A values is a near-unity inverter. */
    triode_build(&sh.t, 280.0, 100e3, 333e3, 2.2e3, 0, 0.3);
    shunt_setup(&sh, 1e6 + 124e3, 1e6, 1e6);
    {
        double g = (shunt_run(&sh, 0.1) - shunt_run(&sh, -0.1)) / 0.2;
        CHECK(g < -0.5 && g > -1.5, "shunt stage gain %.3f (expect about -1)", g);
        int bad = 0;
        for (int i = 0; i < 5000; ++i) bad |= dsp_bad(shunt_run(&sh, 500.0 * sin(i * 0.05)));
        CHECK(!bad, "shunt stage finite under heavy drive");
    }
}

/* --------------------------------------------------------------- linnet */

static void test_linnet(void)
{
    static LinNet n;
    const double fs = 48000.0, R = 1e3, C = 1e-6;

    /* RC low-pass: R as the source resistance into node 1, C to ground. Trapezoidal MNA
     * of this circuit is exactly the bilinear transform of 1 / (1 + sRC). */
    linnet_init(&n, 1, fs);
    linnet_source(&n, 1, R);
    linnet_c(&n, 1, 0, C);
    CHECK(linnet_prepare(&n) == 1, "RC network prepares");
    linnet_reset(&n);
    {
        const double K = 2.0 * fs * R * C, b = 1.0 / (1.0 + K), a1 = (1.0 - K) / (1.0 + K);
        double xp = 0.0, yp = 0.0, err = 0.0, perr = 0.0;
        for (int i = 0; i < 4800; ++i) {
            double x = sin(2 * M_PI * 440.0 * i / fs) + (i > 2400 ? 1.0 : 0.0);
            double k, c0;
            linnet_affine(&n, 1, &k, &c0);
            double pred = k * x + c0;
            double y = linnet_step(&n, x, 1);
            double ref = b * (x + xp) - a1 * yp;
            xp = x; yp = ref;
            err = fmax(err, fabs(y - ref));
            perr = fmax(perr, fabs(y - pred));
        }
        CHECK(err < 1e-9, "RC matches the bilinear one-pole, max err %g", err);
        CHECK(perr < 1e-12, "linnet_affine predicts the step, max err %g", perr);
    }

    /* Resistive divider: 1k source into 1k to ground gives one half. */
    linnet_init(&n, 1, fs);
    linnet_source(&n, 1, 1e3);
    linnet_r(&n, 1, 0, 1e3);
    linnet_prepare(&n);
    linnet_reset(&n);
    CHECK(fabs(linnet_step(&n, 1.0, 1) - 0.5) < 1e-12, "divider");

    /* A floating node has no DC path: the matrix is singular and prepare says so. */
    linnet_init(&n, 2, fs);
    linnet_source(&n, 1, 1e3);
    linnet_r(&n, 1, 0, 1e3);
    CHECK(linnet_prepare(&n) == 0, "singular network is reported");

    /* Changing a value keeps the state: a pot moving mid-signal doesn't reset the caps. */
    linnet_init(&n, 1, fs);
    linnet_source(&n, 1, R);
    linnet_c(&n, 1, 0, C);
    linnet_prepare(&n);
    linnet_reset(&n);
    for (int i = 0; i < 48000; ++i) linnet_step(&n, 1.0, 1);
    linnet_init(&n, 1, fs);
    linnet_source(&n, 1, 2 * R);
    linnet_c(&n, 1, 0, C);
    linnet_prepare(&n);
    CHECK(fabs(linnet_step(&n, 1.0, 1) - 1.0) < 1e-6, "rebuild keeps capacitor state");
}

/* -------------------------------------------------------------- cabinet */

static void test_cabinet(void)
{
    static CabSim c;
    for (int r = 0; r < N_RATES; ++r) {
        const double fs = RATES[r];
        cab_init(&c, fs, 0);
        double ref = 0.0;
        for (int m = 0; m < CAB_MODELS; ++m) {
            Biquad q[CAB_STAGES];
            cab_design_stages(q, m, fs);
            double db = cab_band_db(q, fs) + gain_to_db(c.norm[m]);
            if (m == 0) ref = db;
            CHECK(fabs(db - ref) < 1e-6, "model %d loudness-matched at %.0f Hz (%g dB off)", m, fs, db - ref);
        }
    }

    /* Switching: crossfade lasts CAB_FADE_SEC and then the new bank is heard alone. */
    {
        const double fs = 48000.0;
        cab_init(&c, fs, 0);
        int start = c.cur, bad = 0;
        cab_select(&c, 2);
        CHECK(c.fade_left == (int)(CAB_FADE_SEC * fs), "fade starts");
        for (int i = 0; i < c.fade_len; ++i) bad |= dsp_bad(cab_run(&c, sin(i * 0.1)));
        CHECK(!bad && c.fade_left == 0 && c.cur != start && c.bank[c.cur].model == 2, "fade completes");

        cab_select(&c, CAB_OFF);
        for (int i = 0; i < c.fade_len; ++i) cab_run(&c, 0.0);
        CHECK(cab_run(&c, 0.123) == 0.123, "CAB_OFF is an exact pass-through");
    }
}

/* ------------------------------------------------------------ the chain */
/* A small amp built only from common/: input trim (Smoother) -> 4x oversampled two
 * triode stages -> RC network -> cabinet, with a gain change every block. Run at every
 * rate and a range of block sizes, with silence, sine and full-scale noise. */

typedef struct {
    Oversampler os;
    Triode t1, t2;
    OnePole cpl;
    LinNet net;
    CabSim cab;
    Smoother gain;
} Chain;

static void chain_init(Chain* ch, double fs)
{
    os_init(&ch->os, 4, fs);
    triode_build(&ch->t1, 300.0, 100e3, 1e6, 1.5e3, 1, 0.4);
    triode_build(&ch->t2, 300.0, 100e3, 1e6, 1.5e3, 1, 0.4);
    triode_set_cathode_cap(&ch->t1, 22e-6, ch->os.fs_os);
    triode_set_cathode_cap(&ch->t2, 22e-6, ch->os.fs_os);
    memset(&ch->cpl, 0, sizeof ch->cpl);
    op_set(&ch->cpl, 20.0, ch->os.fs_os);
    linnet_init(&ch->net, 2, fs);
    linnet_source(&ch->net, 1, 47e3);
    linnet_c(&ch->net, 1, 2, 22e-9);
    linnet_r(&ch->net, 2, 0, 250e3);
    linnet_c(&ch->net, 2, 0, 470e-12);
    linnet_prepare(&ch->net);
    linnet_reset(&ch->net);
    cab_init(&ch->cab, fs, 1);
    smoother_init(&ch->gain, 0.010, fs);
}

static double chain_tick(Chain* ch, double x)
{
    double buf[OS_MAX_FACTOR];
    x *= smoother_next(&ch->gain);
    os_up(&ch->os, x, buf);
    for (int k = 0; k < ch->os.factor; ++k) {
        double v = triode_run(&ch->t1, buf[k]);
        v = op_hp(&ch->cpl, v) * 0.05;
        buf[k] = triode_run(&ch->t2, v);
    }
    x = os_down(&ch->os, buf) * 0.01;
    x = linnet_step(&ch->net, x, 2);
    return cab_run(&ch->cab, x);
}

static void test_chain(void)
{
    static const int BLOCKS[] = { 1, 7, 64, 512, 4096 };
    static Chain ch;
    const int nblocks = (int)(sizeof BLOCKS / sizeof BLOCKS[0]);
    unsigned seed = 1;
    for (int r = 0; r < N_RATES; ++r) {
        const double fs = RATES[r];
        for (int b = 0; b < nblocks; ++b) {
            const int n = (int)(fs * 0.25);
            int bad = 0, denorm = 0, blk = 0;
            double peak = 0.0, sil_peak = 0.0;
            DenormalGuard g;
            chain_init(&ch, fs);
            for (int i0 = 0; i0 < 3 * n; i0 += BLOCKS[b], ++blk) {
                const int m = 3 * n - i0 < BLOCKS[b] ? 3 * n - i0 : BLOCKS[b];
                smoother_set(&ch.gain, (blk & 1) ? 1.0 : 0.25); /* parameter change every block */
                denormal_enter(&g);
                for (int i = i0; i < i0 + m; ++i) {
                    double x;
                    if (i < n) {
                        x = 0.5 * sin(2 * M_PI * 220.0 * i / fs);
                    } else if (i < 2 * n) {
                        seed = seed * 1103515245u + 12345u;
                        x = ((seed >> 9) & 0xFFFF) / 32768.0 - 1.0; /* full-scale noise */
                    } else {
                        x = 0.0; /* silence: the tail must decay without denormals */
                    }
                    double y = chain_tick(&ch, x);
                    bad |= dsp_bad(y);
                    denorm |= is_denormal(y);
                    peak = fmax(peak, fabs(y));
                    if (i >= 2 * n + n / 2) sil_peak = fmax(sil_peak, fabs(y));
                }
                denormal_leave(&g);
            }
            CHECK(!bad, "%.0f Hz, block %d: finite", fs, BLOCKS[b]);
            CHECK(!denorm, "%.0f Hz, block %d: no denormal output", fs, BLOCKS[b]);
            CHECK(peak > 1e-3 && peak < 4.0, "%.0f Hz, block %d: peak %g", fs, BLOCKS[b], peak);
            CHECK(sil_peak < 0.05, "%.0f Hz, block %d: tail decays, %g in silence", fs, BLOCKS[b], sil_peak);
        }
    }
}

int main(void)
{
    test_util();
    test_filters();
    test_smooth();
    test_denormal();
    test_oversample();
    test_triode();
    test_linnet();
    test_cabinet();
    test_chain();
    return check_summary("test_dsp");
}
