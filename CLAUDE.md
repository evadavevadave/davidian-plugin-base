# Project rules
- Framework: DPF (submodule in dpf/). Targets: lv2, vst3, clap, au, jack.
- Shared DSP lives in common/ and must not include format headers.
- Audio thread: no allocation, no locks, no I/O, no logging.
- Smooth all parameter changes; handle sampleRateChanged().
- Never change plugin URIs or IDs.
- After changes: build with CMake, run lv2lint and pluginval.
- After DSP changes, run the harness and the SPICE comparison; a failed test blocks the change.

# Repository specifics
- Reference design: docs/ENVIRONMENT.md (Universal Plugin Environment). Reference plugin for
  build, CI, GUI house style and tests: davidian-brownstone-100.
- common/*.h is header-only C99 that must also compile warning-free as C++ (tests/test_cxx.cpp).
  It may include only standard headers, <xmmintrin.h> and its own siblings; never common/ui/,
  DPF, LV2, VST3, CLAP, AU, OpenGL or OS headers. tests/check_headers.cmake enforces this.
- common/ui/ is GUI-thread code (house-style panel). Plugin UI files include
  "ui/dpf_panel_ui.hpp" (the only shared file that includes DPF); DSP files never include ui/.
- Plugins: one folder in plugins/ with one davidian_add_plugin() call (cmake/DavidianPlugin.cmake);
  start from plugins/ExampleGain. Every format comes from DPF, including LV2: no separate
  LV2 C entry points or hand-written .ttl.
- A released plugin has compat.txt (URI, UI URI, CLAP ID, AU/VST3 IDs, LV2 port indices and
  symbols). Never edit it; parameters may only be appended. compat_<name> must pass.
- Functions that build tables or design filters (triode_build, cab_init, os_init, linnet_prepare)
  run at instantiate / sample-rate change, or once per control block while a control moves,
  never per sample. Per-sample functions (*_run, *_step, os_up/os_down) are real-time safe.
- Code moved from a plugin into common/ must not change its sound: prove it with a bit-exact
  render comparison before and after, and report any difference.
- No -ffast-math (it removes NaN guards). Keep the DPF submodule pinned; bump it deliberately.
- Every source file starts with the SPDX GPL-3.0-or-later header and "Copyright (C) 2026 Davidian".
- Build and test: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j &&
  ctest --test-dir build --output-on-failure
