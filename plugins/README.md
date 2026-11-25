# Plugins

One folder per plugin; the top-level CMakeLists.txt adds every folder here that has a
CMakeLists.txt. Start by copying `ExampleGain/`:

| File | Purpose |
| --- | --- |
| `DistrhoPluginInfo.h` | name, URIs, CLAP ID, AU/VST3 IDs, channel counts, UI size |
| `<Name>Params.h` | parameter indices and ranges shared by DSP and UI (append only) |
| `<Name>Plugin.cpp` | DPF plugin: parameters, `run()`, `sampleRateChanged()`, using `common/` |
| `<Name>UI.cpp` | the house-style panel as data, shown by `DavidianPanelUI` |
| `CMakeLists.txt` | one `davidian_add_plugin()` call (+ optional LV2 host tests) |
| `compat.txt` | the released identity; add it at the first release and never edit it |

For a port of an existing plugin, keep its LV2 URI, AU IDs, parameter order and symbols, and
put them in `compat.txt` before changing anything else. See docs/ENVIRONMENT.md, step 4.
