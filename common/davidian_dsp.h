/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Everything in the shared DSP library. Each header can also be included on its own.
 *
 *   dsp_util.h    constants, clamping, dB, pot tapers
 *   smooth.h      parameter smoothing (control-block Glide, per-sample Smoother)
 *   denormal.h    flush-to-zero guard, NaN/inf check
 *   filters.h     TPT one-pole, DF2T biquad, Audio EQ Cookbook designs
 *   oversample.h  1x-16x oversampler for nonlinear stages
 *   triode.h      Koren triode gain stage, shunt-feedback stage
 *   linnet.h      linear circuit solver (MNA with trapezoidal capacitors)
 *   cabinet.h     four loudness-matched speaker-cabinet voicings with crossfade */
#ifndef DAVIDIAN_DSP_H
#define DAVIDIAN_DSP_H

#include "dsp_util.h"
#include "smooth.h"
#include "denormal.h"
#include "filters.h"
#include "oversample.h"
#include "triode.h"
#include "linnet.h"
#include "cabinet.h"

#endif
