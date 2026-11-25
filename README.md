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
      dpf_panel_ui.hpp     the DPF UI class that shows a panel in every format
  cmake/
    DavidianPlugin.cmake davidian_add_plugin(): one call builds every format
    check_*.cmake        per-plugin tests: formats built, released identity, static runtime
    lv2_metadata.cmake   LV2 metadata DPF lacks: released audio symbols, maintainer e-mail
  plugins/
    ExampleGain/         example stereo gain: copy this folder to start a plugin
  tests/               unit tests, panel render, C++ compile test, header rule, LV2 host
    harness/             measurement harness: runs any plugin's DSP with no DAW
  ci/                  fetch_tools.sh, validate.sh, package.sh (used by CI, runnable locally)
  docs/ENVIRONMENT.md  the Universal Plugin Environment design
  CLAUDE.md            rules for Claude Code
  .github/workflows/   CI: builds, tests, validators, sanitizers, release zips
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
| per plugin | see "Tests per plugin" below |

## Measurement harness

`harness_<name>` (run by ctest, report in `build/harness_<name>.md`) drives the plugin through
DPF's own `PluginExporter`, the object every format wrapper uses, at 44.1, 48, 88.2, 96,
176.4 and 192 kHz and block sizes 1, 7, 64, 512, 4096 and random. It fails on:

| Check | How |
| --- | --- |
| NaN / infinity, ceiling, denormals | silence, sine, full-scale noise, impulses, square wave, silence tail; every output sample |
| FP mode | the plugin must leave the host thread's flush-to-zero mode as it found it |
| DC offset | silence and a 1 kHz sine; above -60 dBFS fails |
| clicks | every parameter jumps default -> max -> min -> default on a sine; content above 4 kHz right after a change must not exceed the settled sound by 6 dB and -50 dBFS |
| sample-rate change | after `sampleRateChanged()` the output must match a fresh instance, with every parameter moving |
| CPU | slower than 1.5x real time at any rate (block 512) |
| THD, aliasing | measured with coherent tones (1 kHz at -12 dBFS; 6 kHz at -6 dBFS, non-harmonic energy to 20 kHz); fail only with `--max-thd-db` / `--max-alias-db` |

Per-plugin options go in `HARNESS_ARGS` (e.g. `--allow-step channel` for a switch that is
meant to change instantly). Each check was verified against a deliberately broken copy of
the example plugin.

### Sanitizer builds

```
cmake -S . -B build-asan -DDAVIDIAN_SANITIZE=address,undefined      # GCC or Clang; all tests
CC=clang CXX=clang++ cmake -S . -B build-rtsan -DDAVIDIAN_RTSAN=ON  # Clang 20+; ctest -R harness_
```

With `DAVIDIAN_RTSAN` every block and parameter change the harness sends runs inside a
`[[clang::nonblocking]]` function, so RealtimeSanitizer aborts on any allocation, lock or
system call the plugin makes on the audio thread, with a stack trace.

## Continuous integration

`.github/workflows/build.yml`, on every push:

| Job | Does |
| --- | --- |
| windows | MinGW build, ctest, pluginval (VST3), clap-validator, zips |
| macos | universal build, architecture check, ad-hoc signing, ctest, auval, pluginval (VST3, AU), clap-validator, zips |
| linux x64 / arm64 | build on Ubuntu 22.04, ctest (incl. LV2 host runs), lv2lint, pluginval and clap-validator (x64), zips |
| sanitizers | ASan + UBSan build; all tests |
| rtsan | Clang 20 RealtimeSanitizer build; the harness |
| release | on a `v*` tag: a GitHub release with every zip |

pluginval's GUI tests run as a separate, non-blocking step, as in Brownstone. Each zip,
`davidian-<name>-<version>-<platform>.zip`, holds `LV2/`, `VST3/`, `CLAP/`, `AU/` (macOS),
`Standalone/`, the README and the licences.

### Known upstream warnings

These appear in passing CI runs. They come from DPF (pinned at 4238e1c7), not from plugin
code, and are left as they are rather than carrying a private patch to DPF:

| Where | Message | Why |
| --- | --- | --- |
| clap-validator | `state-invalid-random`: "The plugin loaded random bytes successfully, which is unexpected, but the plugin did not crash" | DPF's CLAP state loader skips data it doesn't recognise and still reports success; nothing is applied, so the plugin's state is unchanged. Not changed in DPF `develop` as of 2026-11. |
| pluginval (VST3) | `DPF warning: asked to delete component while audio processor / connection point still active` | the host releases the VST3 component before the objects it handed out; DPF defers the cleanup to unload. pluginval passes. |

Recheck both when bumping DPF.

## Plugins: one build, every format

Each plugin is a folder under `plugins/` with one call in its `CMakeLists.txt`:

```cmake
davidian_add_plugin(examplegain
  FILES_DSP ExampleGainPlugin.cpp
  FILES_UI  ExampleGainUI.cpp)
```

That single `dpf_add_plugin` call (inside `cmake/DavidianPlugin.cmake`) builds, into `build/bin/`:

| Format | Output | Where |
| --- | --- | --- |
| LV2 | `examplegain.lv2/` (DPF's own LV2 wrapper and generated .ttl) | all |
| VST3 | `examplegain.vst3/` | all |
| CLAP | `examplegain.clap` | all |
| Standalone | `examplegain` / `examplegain.exe`: JACK when running, else CoreAudio, WASAPI, ALSA or PulseAudio | all |
| Audio Unit | `examplegain.component/` | macOS |

On Windows the compiler runtime is linked statically (MinGW `-static`, MSVC `/MT`), so
the standalone and the plugins need no installer or redistributable; a test checks the
DLL imports of every binary.

### Keeping existing sessions working

Hosts find a plugin and its controls by identity, so a ported plugin keeps its released one:

- `DistrhoPluginInfo.h`: `DISTRHO_PLUGIN_URI` (the old LV2 URI), `DISTRHO_UI_URI`,
  `DISTRHO_PLUGIN_CLAP_ID`, and `DISTRHO_PLUGIN_BRAND_ID` / `DISTRHO_PLUGIN_UNIQUE_ID`
  (the AU manufacturer and subtype codes, which also make the VST3 class ID).
- LV2 control ports are the audio ports followed by the parameters in order, with the
  parameter symbols: keep the old order and symbols.
- DPF names LV2 audio ports `lv2_<symbol>`; `LV2_AUDIO_SYMBOLS in out` restores released
  audio symbols in the generated metadata.
- `compat.txt` in the plugin folder records all of the above; the `compat_<name>` test fails
  if any of it changes.

### Tests per plugin

`harness_<name>` (see above), `formats_<name>` (every format built), `compat_<name>` (released identity unchanged),
`static_runtime_<name>` (Windows), and with lilv installed `lv2_<name>_44100` /
`lv2_<name>_192000` (the bundle loads in an LV2 host and produces clean audio).
`davidian_add_lv2_test()` adds runs with chosen control values, as the example does to
prove its gain.

### Code

```c
#include "davidian_dsp.h"         /* DSP */
#include "ui/dpf_panel_ui.hpp"    /* UI file only: DavidianPanelUI + the panel (namespace dui) */
```

## Licence

GPL-3.0-or-later (see `LICENSE`). DPF is ISC-licensed (see `LICENSE-DPF`).

### References

1. N. Koren, "Improved vacuum tube models for SPICE simulations", Glass Audio 8(5), 1996.
2. C.-W. Ho, A. E. Ruehli and P. A. Brennan, "The modified nodal approach to network analysis",
   IEEE Trans. Circuits and Systems 22(6), 1975.
3. R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients".
4. V. Zavalishin, "The Art of VA Filter Design", Native Instruments, 2012-2020.
