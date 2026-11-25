/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Spectrum analysis for the harness: FFT, coherent test tones, THD and aliasing.
 *
 * Test tones are placed exactly on an FFT bin (coherent sampling), so a cosine-sum
 * window confines each tone to a few bins and the measurement floor is set by the
 * plugin's float output (about -140 dB), not by window leakage. */
#ifndef DAVIDIAN_HARNESS_ANALYSIS_H
#define DAVIDIAN_HARNESS_ANALYSIS_H

#include <cmath>
#include <complex>
#include <vector>

namespace analysis {

static const double PI = 3.14159265358979323846;

/* In-place radix-2 FFT; x.size() must be a power of two. */
inline void fft(std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * PI / (double)len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/* Power spectrum (bins 0 .. n/2) of x with a 4-term Blackman-Harris window. A tone on
 * an exact bin occupies that bin +/- 3. */
inline std::vector<double> power_spectrum(const float* x, size_t n)
{
    static const double a[4] = { 0.35875, 0.48829, 0.14128, 0.01168 };
    std::vector<std::complex<double>> v(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = 2.0 * PI * (double)i / (double)n;
        const double w = a[0] - a[1] * std::cos(t) + a[2] * std::cos(2 * t) - a[3] * std::cos(3 * t);
        v[i] = std::complex<double>(x[i] * w, 0.0);
    }
    fft(v);
    std::vector<double> p(n / 2 + 1);
    for (size_t i = 0; i <= n / 2; ++i) p[i] = std::norm(v[i]);
    return p;
}

/* An odd FFT bin near `freq` (odd bins keep folded harmonics off the true ones). */
inline size_t coherent_bin(double freq, double rate, size_t n)
{
    size_t k = (size_t)std::floor(freq * (double)n / rate + 0.5);
    if (k % 2 == 0) ++k;
    return k;
}

/* Power of a tone at bin k: the bins it can occupy with the window. */
inline double tone_power(const std::vector<double>& p, size_t k)
{
    double s = 0.0;
    for (size_t i = k > 3 ? k - 3 : 0; i <= k + 3 && i < p.size(); ++i) s += p[i];
    return s;
}

inline double to_db(double power_ratio)
{
    return 10.0 * std::log10(power_ratio > 1e-30 ? power_ratio : 1e-30);
}

struct Distortion {
    double fund_power;
    double thd_db;      /* harmonics 2..10 (in band) relative to the fundamental */
    double h_db[11];    /* each harmonic relative to the fundamental; -300 if out of band */
};

/* Harmonic distortion of a tone at bin k. */
inline Distortion distortion(const std::vector<double>& p, size_t k)
{
    Distortion d;
    const size_t nyq = p.size() - 1;
    d.fund_power = tone_power(p, k);
    double sum = 0.0;
    for (int h = 0; h <= 10; ++h) d.h_db[h] = -300.0;
    for (size_t h = 2; h <= 10; ++h) {
        if (h * k + 3 >= nyq) break;
        const double ph = tone_power(p, h * k);
        sum += ph;
        d.h_db[h] = to_db(ph / d.fund_power);
    }
    d.thd_db = to_db(sum / d.fund_power);
    return d;
}

/* Energy that is not the tone, its in-band harmonics or DC, between 20 Hz and `top`,
 * relative to the fundamental. With a clean input this is aliasing plus noise. */
inline double non_harmonic_db(const std::vector<double>& p, size_t k, double rate, double top)
{
    const size_t n = (p.size() - 1) * 2;
    const size_t lo = (size_t)std::ceil(20.0 * (double)n / rate) + 4;
    size_t hi = (size_t)std::floor(top * (double)n / rate);
    if (hi > p.size() - 1) hi = p.size() - 1;
    double sum = 0.0;
    for (size_t i = lo; i <= hi; ++i) {
        const size_t h = (i + k / 2) / k; /* nearest harmonic number */
        const size_t dist = i > h * k ? i - h * k : h * k - i;
        if (h >= 1 && dist <= 4) continue;
        sum += p[i];
    }
    return to_db(sum / tone_power(p, k));
}

} // namespace analysis

#endif
