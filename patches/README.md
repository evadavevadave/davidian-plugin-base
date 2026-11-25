# DPF patches

Fixes for DPF bugs that affect Davidian plugins and are not fixed upstream yet. The `dpf/`
submodule stays pinned and unmodified: at configure time CMake copies DPF to
`build/dpf-patched/` and applies every `*.patch` here with `git apply`
(`-DDAVIDIAN_DPF_PATCHES=OFF` builds the unpatched submodule instead).

Rules:

- A patch exists only for a real bug with a test that fails without it.
- Each patch is reported upstream, and removed once the DPF pin includes the fix. When
  DPF is bumped, a patch that no longer applies makes configuring fail with a message
  saying so.
- Behaviour that is only noisy (warnings) is not patched; see "Known upstream warnings"
  in the top-level README.

| Patch | Bug | Test | Upstream |
| --- | --- | --- | --- |
| `dpf-state-chunked-read.patch` | CLAP `stateLoad` and VST3 `setState` drop or mangle parameters when the host passes saved state in small reads | `state_clap_<name>`, `state_vst3_<name>` (`tests/plugin_state.cpp`) | not fixed in `develop` as of 2026-11; report drafted in `dpf-state-chunked-read-issue.md`, to be filed |

## dpf-state-chunked-read.patch

DPF reads saved state in up to 511-byte reads and splits it at `'\0'` separators. After
taking the string that starts at the current position, it checks whether the next byte is
`'\0'` to decide that the string is complete. When the string runs to the end of the
bytes just read, that "next byte" lies past the read: stale data from an earlier, longer
read (or uninitialized stack). If it happens to be zero, a key or value cut by the read
boundary is taken as complete; the following piece then becomes a separate string, keys
and values fall out of step, and parameters are restored wrong or not at all.

clap-validator's `state-reproducibility-buffered` test found it in CI (it reads in small
prime-sized pieces; whether the stale byte is zero varies, so it failed intermittently).
`tests/plugin_state.cpp` reproduces it every time by zeroing the reader's buffer past
each piece.

The fix only accepts a terminator inside the bytes actually read (`i < read &&`), in both
`DistrhoPluginCLAP.cpp` and `DistrhoPluginVST3.cpp`.
