# Davidian Plugin Base

Shared foundation for Davidian audio plugins: write each plugin's DSP once and build LV2,
VST3, CLAP, Audio Unit and a standalone JACK app from the same source with the
[DISTRHO Plugin Framework](https://github.com/DISTRHO/DPF) (DPF).

The design follows [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md). Build setup, CI, GUI house
style and tests follow Davidian Brownstone 100, whose DSP and panel code this repository's
`common/` was extracted from.

## Layout

```
davidian-plugin-base/
  dpf/                 DPF, git submodule pinned at 4238e1c7 (same as Brownstone 100)
  common/              shared DSP: header-only C99, also valid C++, no format or GUI headers
    davidian_dsp.h       includes everything below
    dsp_util.h           constants, clamping, dB, pot tapers
    smooth.h             parameter smoothing: control-block Glide, per-sample Smoother
    denormal.h           flush-to-zero guard (x86 FTZ/DAZ, ARM64 FZ), NaN/inf check
    filters.h            TPT one-pole, DF2T biquad, Audio EQ Cookbook and Butterworth designs
    oversample.h         1x/2x/4x/8x/16x oversampler for nonlinear stages
    triode.h             Koren triode gain stage (12AX7), shunt-feedback stage
    linnet.h             linear circuit solver: nodal analysis, trapezoidal capacitors
    cabinet.h            four loudness-matched speaker-cabinet voicings with crossfade
    ui/                  house-style GUI (GUI thread only)
      style.h              palette and lettering sizes
      font.h               stroke font
      canvas.h             anti-aliased software renderer
      panel.h              knobs, trims, switches, footswitches, selectors; layout as data
  plugins/             one folder per plugin (none yet)
  tests/               unit tests, panel render, C++ compile test, header rule (CTest)
  docs/ENVIRONMENT.md  the Universal Plugin Environment design
  CLAUDE.md            rules for Claude Code
  .github/workflows/   CI: Windows, macOS (universal), Linux x64 and arm64
```

## Building

Gentoo dependencies are listed in docs/ENVIRONMENT.md section 2.

```
git clone --recurse-submodules <repo-url> davidian-plugin-base
cd davidian-plugin-base
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`ctest` runs:

| Test | Checks |
| --- | --- |
| `dsp` | each DSP module against analytic results (filter responses, RC network vs. bilinear one-pole, 12AX7 operating point and gain, cabinet loudness match, oversampler passband and aliasing), then a small amp chain at 44.1 to 192 kHz with block sizes 1 to 4096: no NaN, infinity or denormals, tails decay |
| `panel` | renders every widget kind to `build/panel.bmp`, and checks redraw regions, value handling, mouse drag, fine drag, double-click reset, switches, selector and wheel |
| `cxx` | every shared header compiles and runs as C++ |
| `dsp_headers_format_free` | no file in `common/` includes a plugin-format, GUI or OS header, and DSP never includes `common/ui/` |

## Using it from a plugin

```cmake
dpf_add_plugin(MyAmp
  TARGETS lv2 vst3 clap jack
  FILES_DSP MyAmpPlugin.cpp
  FILES_UI  MyAmpUI.cpp)
target_link_libraries(MyAmp PUBLIC davidian::dsp davidian::ui)
```

```c
#include "davidian_dsp.h"   /* DSP */
#include "ui/panel.h"       /* UI file only */
```

## Licence

GPL-3.0-or-later (see `LICENSE`). DPF is ISC-licensed (see `LICENSE-DPF`).

### References

1. N. Koren, "Improved vacuum tube models for SPICE simulations", Glass Audio 8(5), 1996.
2. C.-W. Ho, A. E. Ruehli and P. A. Brennan, "The modified nodal approach to network analysis",
   IEEE Trans. Circuits and Systems 22(6), 1975.
3. R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients".
4. V. Zavalishin, "The Art of VA Filter Design", Native Instruments, 2012-2020.
