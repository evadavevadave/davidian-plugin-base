/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Denormal and NaN protection for the audio thread.
 *
 * Decaying filter and feedback states drift into denormal numbers, which are very slow
 * on most CPUs. DenormalGuard turns on flush-to-zero (and denormals-are-zero on x86) for
 * the duration of a process call and restores the host's setting afterwards:
 *
 *     DenormalGuard g;
 *     denormal_enter(&g);
 *     ... process ...
 *     denormal_leave(&g);
 *
 * x86 / x86-64: MXCSR FTZ | DAZ (as in Brownstone).  AArch64: FPCR.FZ.
 * Elsewhere it does nothing, and the code must still be correct, only slower. */
#ifndef DAVIDIAN_DENORMAL_H
#define DAVIDIAN_DENORMAL_H

#include <stdint.h>

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE__) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#  include <xmmintrin.h>
#  define DAVIDIAN_DENORMAL_SSE 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#  define DAVIDIAN_DENORMAL_ARM64 1
#endif

typedef struct {
    uint64_t saved;
} DenormalGuard;

static inline void denormal_enter(DenormalGuard* g)
{
#if defined(DAVIDIAN_DENORMAL_SSE)
    unsigned int csr = _mm_getcsr();
    g->saved = csr;
    _mm_setcsr(csr | 0x8040); /* FTZ | DAZ */
#elif defined(DAVIDIAN_DENORMAL_ARM64)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    g->saved = fpcr;
    fpcr |= (uint64_t)1 << 24; /* FZ */
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#else
    g->saved = 0;
#endif
}

static inline void denormal_leave(const DenormalGuard* g)
{
#if defined(DAVIDIAN_DENORMAL_SSE)
    _mm_setcsr((unsigned int)g->saved);
#elif defined(DAVIDIAN_DENORMAL_ARM64)
    __asm__ __volatile__("msr fpcr, %0" : : "r"(g->saved));
#else
    (void)g;
#endif
}

/* 1 if flush-to-zero is active on this thread (for tests). */
static inline int denormal_ftz_active(void)
{
#if defined(DAVIDIAN_DENORMAL_SSE)
    return (_mm_getcsr() & 0x8000) != 0;
#elif defined(DAVIDIAN_DENORMAL_ARM64)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    return (fpcr >> 24) & 1;
#else
    return 0;
#endif
}

/* Flushes a value below ~1e-30 to zero: for states that must stay clean on platforms
 * without hardware flush-to-zero. */
static inline double undenormal(double x)
{
    return (x > -1e-30 && x < 1e-30) ? 0.0 : x;
}

/* True for NaN and +/-infinity. Written without isfinite() so it still works if a
 * plugin is ever built with -ffinite-math-only (which must not be used; see CLAUDE.md). */
static inline int dsp_bad(double x)
{
    return !(x == x) || x - x != 0.0;
}

#endif
