# Universal Plugin Environment with DPF

Goal: write each plugin's DSP once, then build LV2, VST3, CLAP, AU and a standalone JACK app from the same source.

## 1. Repository layout

```
my-plugins/
  dpf/                  (DPF as a git submodule)
  common/               (shared DSP: filters, gain smoothing, oversampling, utils)
  plugins/
    AmpSim/
      DistrhoPluginInfo.h
      AmpSimPlugin.cpp
      AmpSimUI.cpp      (optional)
      CMakeLists.txt or Makefile
  tests/
  CMakeLists.txt
  CLAUDE.md
  .github/workflows/build.yml
```

Set it up:

```
git init my-plugins && cd my-plugins
git submodule add https://github.com/DISTRHO/DPF.git dpf
git submodule update --init --recursive
```

## 2. Gentoo build dependencies

```
emerge --ask dev-build/cmake virtual/pkgconfig media-libs/lv2 \
  virtual/jack media-libs/mesa x11-libs/libX11 x11-libs/libXext \
  x11-libs/libXrandr x11-libs/libXcursor
```

Exact package names can shift between Gentoo snapshots, so check with `eix` or `emerge -s` if one fails.

## 3. Top-level CMakeLists.txt

```
cmake_minimum_required(VERSION 3.15)
project(MyPlugins)
add_subdirectory(dpf)
add_subdirectory(plugins/AmpSim)
```

## 4. Per-plugin CMakeLists.txt

```
dpf_add_plugin(AmpSim
  TARGETS lv2 vst3 clap jack au
  FILES_DSP
    AmpSimPlugin.cpp
    ../../common/Dsp.cpp
  FILES_UI
    AmpSimUI.cpp)

target_include_directories(AmpSim PUBLIC . ../../common)
```

The `au` target only builds on macOS. On Linux, drop it from the list or guard it with `if(APPLE)`.

## 5. DistrhoPluginInfo.h essentials

```
#define DISTRHO_PLUGIN_BRAND   "YourBrand"
#define DISTRHO_PLUGIN_NAME    "AmpSim"
#define DISTRHO_PLUGIN_URI     "https://yourdomain/plugins/ampsim"
#define DISTRHO_PLUGIN_CLAP_ID "com.yourbrand.ampsim"
#define DISTRHO_PLUGIN_BRAND_ID  Ybrd   // 4 chars, AU and VST3
#define DISTRHO_PLUGIN_UNIQUE_ID Amps   // 4 chars, unique per plugin
#define DISTRHO_PLUGIN_NUM_INPUTS  1
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2
#define DISTRHO_PLUGIN_HAS_UI      1
#define DISTRHO_PLUGIN_IS_RT_SAFE  1
```

Never change the URI, CLAP ID, brand ID or unique ID after release, or hosts will treat it as a different plugin and saved sessions will break.

## 6. Building on Linux

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Outputs go to `build/bin/`: `.lv2` bundles, `.vst3`, `.clap` and the JACK standalone.

## 7. Getting AU builds

AU needs macOS and Xcode. Realistic options are a Mac, or a GitHub Actions workflow on a `macos-latest` runner that runs the same CMake commands. That also gets you macOS VST3 and CLAP. Add a `windows-latest` job for Windows builds too.

## 8. Validation tools

- LV2: `lv2lint` and testing in Carla or Jalv
- VST3: Steinberg's validator and `pluginval`
- CLAP: `clap-validator`
- AU: `auval -a`, then `auval -v aufx Amps Ybrd` on macOS
- Everything: load each format in REAPER or Carla and test state save/restore, sample-rate changes and automation

## 9. Porting existing LV2 plugins

1. Move pure DSP (processing math, filters) into `common/` with no LV2 headers.
2. Map each LV2 control port to a DPF parameter in `initParameter()`.
3. Move `run()` logic into `run(const float** inputs, float** outputs, uint32_t frames)`.
4. Move instantiation and sample-rate setup into the constructor and `sampleRateChanged()`.
5. Keep the old LV2 URI as `DISTRHO_PLUGIN_URI` so existing Carla sessions keep working.

## 10. CLAUDE.md for Claude Code

```
# Project rules
- Framework: DPF (submodule in dpf/). Targets: lv2, vst3, clap, au, jack.
- Shared DSP lives in common/ and must not include format headers.
- Audio thread: no allocation, no locks, no I/O, no logging.
- Smooth all parameter changes; handle sampleRateChanged().
- Never change plugin URIs or IDs.
- After changes: build with CMake, run lv2lint and pluginval.
```

## 11. Quality environment

Four layers catch problems before you hear them: reference comparison, a measurement harness, real-time safety checks and CI.

### Reference comparison against SPICE

1. Draw or import each schematic into ngspice (LTspice on Windows also works).
2. Run a transient analysis with standard test signals: 1 kHz sines at several levels, a log sweep and a short DI guitar clip.
3. Export the simulated output as WAV at 96 kHz or higher.
4. Render the same signals through the plugin with matching knob settings.
5. A script (Python with numpy, scipy and soundfile) compares the two: frequency response error in dB, harmonic levels, clipping onset level and an RMS null difference.
6. Store the SPICE renders in `tests/reference/` so every build is checked against them.

### Measurement harness

A small offline host in `tests/harness/` that loads the DSP core directly, no DAW needed:

- Signals: sine steps, log sweeps, impulses, silence, full-scale noise and real DI recordings
- Reports: THD and harmonic profile, aliasing (energy at non-harmonic frequencies on a high sine), latency, CPU time per block
- Fails on: NaN or infinity, denormals, DC offset, output above a set ceiling, clicks on parameter changes
- Runs at 44.1, 48, 96 and 192 kHz and block sizes from 1 to 4096

Testing the shared `common/` core rather than each format keeps one harness for every plugin.

### Real-time safety

- Build a test configuration with Clang and `-fsanitize=realtime` (RealtimeSanitizer, Clang 20 or newer); it aborts on allocation, locks or system calls inside functions marked real-time.
- Also run AddressSanitizer and UndefinedBehaviorSanitizer builds through the harness.
- Add a CPU budget test: processing must stay well under the block's real-time duration.

### CI on every push

GitHub Actions with three jobs:

| Runner | Builds | Checks |
| --- | --- | --- |
| ubuntu-latest | LV2, VST3, CLAP, JACK | lv2lint, pluginval, clap-validator, harness, SPICE comparison, sanitizers |
| windows-latest | VST3, CLAP | pluginval, harness |
| macos-latest | AU, VST3, CLAP | auval, pluginval, harness |

Built plugins are uploaded as artifacts, so you can download and test the AU and Windows builds without owning a Mac.

### Listening check

Automated tests catch errors, not tone. Keep a fixed set of DI clips and do blind A/B comparisons against the SPICE render or the real hardware in Carla before each release.

Add to CLAUDE.md: `After DSP changes, run the harness and the SPICE comparison; a failed test blocks the change.`

## 12. GitHub Actions workflow

Save as `.github/workflows/build.yml`. Every push builds Linux, Windows and macOS and uploads the plugins as downloadable artifacts. The macOS job is how you get AU without a Mac.

```
name: build

on:
  push:
  pull_request:

jobs:
  linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake pkg-config lv2-dev libjack-jackd2-dev \
            libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libdbus-1-dev
      - name: Configure and build
        run: |
          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build -j$(nproc)
      - name: Validate with pluginval
        run: |
          curl -L -o pluginval.zip https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Linux.zip
          unzip pluginval.zip
          for p in build/bin/*.vst3; do ./pluginval --strictness-level 5 --validate "$p"; done
      - name: Run harness tests
        run: ctest --test-dir build --output-on-failure
      - uses: actions/upload-artifact@v4
        with:
          name: plugins-linux
          path: build/bin/

  windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Configure and build
        run: |
          cmake -S . -B build
          cmake --build build --config Release
      - name: Run harness tests
        run: ctest --test-dir build -C Release --output-on-failure
      - uses: actions/upload-artifact@v4
        with:
          name: plugins-windows
          path: build/bin/

  macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Configure and build (universal binary)
        run: |
          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
          cmake --build build -j$(sysctl -n hw.ncpu)
      - name: Validate AU
        run: |
          mkdir -p ~/Library/Audio/Plug-Ins/Components
          cp -R build/bin/*.component ~/Library/Audio/Plug-Ins/Components/
          killall -9 AudioComponentRegistrar || true
          auval -a
          auval -v aufx Amps Ybrd
      - name: Run harness tests
        run: ctest --test-dir build --output-on-failure
      - uses: actions/upload-artifact@v4
        with:
          name: plugins-macos
          path: build/bin/
```

Notes:

- Replace `aufx Amps Ybrd` with each plugin's type, unique ID and brand ID; use `aumu` for instruments.
- The `ctest` steps only run once the harness is registered with `add_test()` in CMake; remove them until then.
- Check the pluginval download name against its current release page if that step fails.
- Unsigned macOS builds will be blocked by Gatekeeper on other Macs; testers can clear it with `xattr -cr`, or you can add code signing later.

## 13. Licensing

All of the stack works with GPL3: DPF is ISC, CLAP is MIT, and the VST3 SDK is GPL3-compatible. Put a `LICENSE` file with the GPL3 text at the repo root and a short licence header in each source file.

## 14. Feature plan: matching commercial plugins

Build these once in `common/` so every Davidian plugin gets them, in the order below: sound first, then playability, then workflow.

### Phase 1: sound quality

1. **Selectable oversampling.** Replace the fixed setting with a user choice: 1x, 2x, 4x, 8x and 16x, plus a separate higher setting used automatically for offline rendering.
2. **Oversample only nonlinear stages.** Tone stacks and other linear parts run at the base rate; only clipping and valve stages are oversampled, which cuts CPU sharply.
3. **ADAA (antiderivative anti-aliasing).** Apply to diode clippers and waveshapers so 2x or 4x sounds as clean as much higher factors.
4. **Two filter modes.** Low-latency minimum-phase (polyphase IIR) for live playing; linear-phase FIR for mixing. Report latency to the host correctly in both.
5. **Harness tests** measure aliasing at each setting so improvements are proven, not assumed.

### Phase 2: playability

1. **Input gain with metering.** Calibrate 0 dBFS to a stated guitar voltage, show an input meter with a target zone, and add output level metering.
2. **Cab section.** An impulse response loader (WAV files), with bundled IRs and a bypass for players using their own cab or loader.

Deferred: a noise gate and tuner. Most players already have these in their DAW or on their pedalboard; revisit later if users ask.

### Phase 3: workflow

1. **Presets.** Factory presets per plugin plus user save and load, stored in a format-independent file.
2. **A/B compare and undo.** Two settings slots with a switch, and undo/redo for knob changes.
3. **Resizable, high-DPI UI.** Built in DPF's cross-platform UI, replacing the native Win32 interfaces.
4. **MIDI learn.** Map footswitches and expression pedals to any parameter, saved with the session.

### Phase 4: user manuals

A short manual for every plugin, built from one shared template so they look consistent:

1. **Format.** A one- to four-page PDF per plugin, generated from Markdown in each repo (for example with Pandoc) by the release workflow, so it ships in every download zip.
2. **Contents.** What the plugin models, installation per format, each control explained in plain words, suggested starting settings for a few styles, and troubleshooting (macOS unblocking, host rescans).
3. **Reuse the READMEs.** The control tables and modelling notes already in each README become the technical appendix; the manual leads with playing, not circuit detail.
4. **Screenshots.** The GUI image generated by the UI render test, so screenshots never go out of date.

Open question: which plugin goes first. Canary Plus is the simplest test of the full pipeline.

## 15. Claude Code prompts: step by step

Paste these into Claude Code one at a time, in order. Finish and check each step before starting the next.

### Step 1: create the shared base

```
Create a new repo called davidian-plugin-base, following the Universal Plugin Environment doc.
Use davidian-brownstone-100 as the reference for build, CI, GUI house style and tests.
- Add DPF as a pinned git submodule.
- Create common/ for shared DSP: parameter smoothing, denormal guards, oversampling,
  Koren triode stage, linear network solver (from Brownstone's linnet.h), cabinet filters.
- Create common/ui/ from Brownstone's house-style GUI (style, font, canvas, panel helpers).
- Add the CLAUDE.md rules from the doc.
- Keep DSP free of any format or GUI headers.
Do not change any plugin repos yet. Show me the layout and build it before finishing.
```

### Step 2: one build for every format

```
In davidian-plugin-base, set up the CMake template so one dpf_add_plugin call builds
LV2, VST3, CLAP, standalone (jack) and AU (macOS only) from the same code.
- LV2 must come from DPF, not a separate C entry point.
- Each plugin must be able to keep its existing LV2 URI and AU IDs, so old sessions load.
- Link the Windows runtime statically so the standalone needs no installer.
Add a tiny example gain plugin to prove all formats build.
```

### Step 3: CI and quality checks

```
Add GitHub Actions to davidian-plugin-base, based on Brownstone's workflow:
- Build on Windows, macOS (universal), Linux x64 and Linux arm64.
- Run pluginval, auval, lv2lint and clap-validator.
- Add a Clang build with RealtimeSanitizer, plus ASan and UBSan runs of the tests.
- Add harness tests: NaN, denormals, DC offset, clicks on parameter changes,
  aliasing and THD measurements at 44.1 to 192 kHz and block sizes 1 to 4096.
- Upload every format as release artifacts.
```

### Step 4: port Canary Plus

```
Port davidian-canary-plus onto davidian-plugin-base.
- Move its DSP into the shared structure without changing the sound.
- Keep its LV2 URI and existing parameter order and ranges.
- Replace the Win32-only GUI with the shared cross-platform house-style GUI.
- Before and after the port, render the same test signals and confirm the outputs match
  (null test), and report any difference.
- Update the README to list all formats.
```

### After step 4

Check Canary Plus in Carla, REAPER and the standalone app. If it is right, port the other plugins the same way, one per session, then start the feature phases.
