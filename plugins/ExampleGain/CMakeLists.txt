# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# One call builds LV2, VST3, CLAP, the standalone app and (on macOS) AU.
# LV2_AUDIO_SYMBOLS keeps a released plugin's LV2 audio port symbols; it is used here to
# exercise that path (DPF's own names would be lv2_audio_in_1 ...).

davidian_add_plugin(examplegain
  FILES_DSP ExampleGainPlugin.cpp
  FILES_UI  ExampleGainUI.cpp
  LV2_AUDIO_SYMBOLS in_l in_r out_l out_r
  # A clean gain: any distortion or aliasing above float rounding is a bug.
  HARNESS_ARGS --max-thd-db -120 --max-alias-db -120)

# The gain really is applied, in every LV2 host: -6.0206 dB is x0.5, 0 dB is unity and the
# bottom of the range is silence.
davidian_add_lv2_test(examplegain lv2_examplegain_minus6db gain=-6.0206 --expect-gain 0.5)
davidian_add_lv2_test(examplegain lv2_examplegain_unity gain=0 --expect-gain 1)
davidian_add_lv2_test(examplegain lv2_examplegain_off gain=-60 --expect-gain 0)
