# Plugins

One folder per plugin, each with `DistrhoPluginInfo.h`, its plugin and UI sources and a
`CMakeLists.txt` that calls `dpf_add_plugin()` and links `davidian::dsp` (and
`davidian::ui` for the GUI). The top-level CMakeLists.txt adds every folder here that has
a CMakeLists.txt. See docs/ENVIRONMENT.md, steps 2 and 4.
