#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Packages every format of every plugin in build/plugins.tsv into one zip per plugin
# and platform, as Brownstone's release zips:
#   build/dist/davidian-<name>-<version>-<platform>.zip
#     LV2/<name>.lv2  VST3/<name>.vst3  CLAP/<name>.clap  AU/<name>.component (macOS)
#     Standalone/<name>[.exe]  README.md  LICENSE  LICENSE-DPF  (+ PACKAGE_FILES)
#   ci/package.sh <version> <platform>      e.g. ci/package.sh v1.0.0 macos-universal
# Run from the repository root (the base itself, or a plugin repository using it as base/).
set -euo pipefail

version="$1"
platform="$2"
bin="build/bin"
base="$(cd "$(dirname "$0")/.." && pwd)"  # this script's repository: the base
mkdir -p build/dist

while IFS=$'\t' read -r name uri au_type au_sub au_manu extra; do
  [ -z "$name" ] && continue
  pkg="davidian-${name}-${version}-${platform}"
  out="build/dist/$pkg"
  rm -rf "$out" "build/dist/$pkg.zip"
  mkdir -p "$out/LV2" "$out/VST3" "$out/CLAP" "$out/Standalone"

  cp -R "$bin/$name.lv2" "$out/LV2/"
  cp -R "$bin/$name.vst3" "$out/VST3/"
  cp -R "$bin/$name.clap" "$out/CLAP/"
  if [ -d "$bin/$name.component" ]; then
    mkdir -p "$out/AU"
    cp -R "$bin/$name.component" "$out/AU/"
  fi
  if [ -f "$bin/$name.exe" ]; then
    cp "$bin/$name.exe" "$out/Standalone/"
  elif [ -d "$bin/$name.app" ]; then
    cp -R "$bin/$name.app" "$out/Standalone/"
  else
    cp "$bin/$name" "$out/Standalone/"
  fi
  # A plugin folder's own README wins over the repository README.
  readme="README.md"
  dir=$(grep -rl --include=CMakeLists.txt "davidian_add_plugin($name\b" . 2> /dev/null | grep -v '^\./build' | head -n 1 || true)
  if [ -n "$dir" ] && [ -f "$(dirname "$dir")/README.md" ] && [ "$(dirname "$dir")" != "." ]; then
    readme="$(dirname "$dir")/README.md"
  fi
  cp "$readme" "$out/README.md"
  cp LICENSE "$out/"
  # In a plugin repository the DPF licence comes with the base submodule.
  if [ -f LICENSE-DPF ]; then cp LICENSE-DPF "$out/"; else cp "$base/LICENSE-DPF" "$out/"; fi
  # PACKAGE_FILES from davidian_add_plugin()
  if [ -n "${extra:-}" ]; then
    IFS='|' read -r -a files <<< "$extra"
    for f in "${files[@]}"; do cp "$f" "$out/"; done
  fi

  (
    cd build/dist
    if command -v zip > /dev/null 2>&1; then
      zip -qry "$pkg.zip" "$pkg"
    else
      cmake -E tar cf "$pkg.zip" --format=zip "$pkg"
    fi
  )
  echo "Packaged build/dist/$pkg.zip ($uri)"
done < build/plugins.tsv
