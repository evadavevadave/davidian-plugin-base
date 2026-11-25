#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Runs the plugin validators on every plugin listed in build/plugins.tsv.
#   ci/validate.sh <linux|macos|windows> [--gui]
#
#   pluginval --strictness-level 5   VST3 (all) and AU (macOS)
#   clap-validator validate          CLAP
#   auval -strict                    AU (macOS)
#   lv2lint -M nopack                LV2 (when lv2lint is on PATH; built by the Linux job).
#                                    nopack mode adds the checks for binaries the developer
#                                    distributes; fails block, warnings are shown
#
# --gui runs pluginval with its GUI tests instead (opens each editor); CI runs that as a
# separate, non-blocking step, as Brownstone does, because headless runners are flaky.
# Expects ci/fetch_tools.sh to have run. Exits non-zero if any validator fails.
set -uo pipefail

os="$1"
gui="${2:-}"
bin="build/bin"
tools="build/tools"
failed=()

case "$os" in
  linux)   PV="$tools/pluginval/pluginval";                            CV="$tools/clap-validator/clap-validator" ;;
  macos)   PV="$tools/pluginval/pluginval.app/Contents/MacOS/pluginval"; CV="$tools/clap-validator/clap-validator" ;;
  windows) PV="$tools/pluginval/pluginval.exe";                        CV="$tools/clap-validator/clap-validator.exe" ;;
  *) echo "usage: ci/validate.sh <linux|macos|windows> [--gui]"; exit 2 ;;
esac

run() { # label, command...
  local label="$1"; shift
  echo "::group::$label"
  "$@"
  local rc=$?
  echo "::endgroup::"
  if [ $rc -ne 0 ]; then
    echo "::error::$label failed ($rc)"
    failed+=("$label")
  else
    echo "ok: $label"
  fi
}

pv_flags=(--strictness-level 5 --validate-in-process)
if [ "$gui" != "--gui" ]; then pv_flags+=(--skip-gui-tests); fi

while IFS=$'\t' read -r name uri au_type au_sub au_manu extra; do
  [ -z "$name" ] && continue

  if [ "$gui" = "--gui" ]; then
    run "pluginval GUI $name.vst3" "$PV" "${pv_flags[@]}" --validate "$bin/$name.vst3"
    if [ "$os" = macos ]; then
      run "pluginval GUI $name.component" "$PV" "${pv_flags[@]}" \
        --validate "$HOME/Library/Audio/Plug-Ins/Components/$name.component"
    fi
    continue
  fi

  run "pluginval $name.vst3" "$PV" "${pv_flags[@]}" --validate "$bin/$name.vst3"
  run "clap-validator $name.clap" "$CV" validate "$bin/$name.clap"

  if [ "$os" = macos ]; then
    # AU hosts only see components installed in a Components folder.
    comp="$HOME/Library/Audio/Plug-Ins/Components"
    mkdir -p "$comp"
    rm -rf "$comp/$name.component"
    cp -R "$bin/$name.component" "$comp/"
    killall -9 AudioComponentRegistrar 2> /dev/null || true
    sleep 3
    run "auval $au_type $au_sub $au_manu" auval -strict -v "$au_type" "$au_sub" "$au_manu"
    run "pluginval $name.component" "$PV" "${pv_flags[@]}" --validate "$comp/$name.component"
  fi

  if command -v lv2lint > /dev/null 2>&1; then
    run "lv2lint $name.lv2" lv2lint -M nopack -I "$PWD/$bin/$name.lv2/" "$uri"
  fi
done < build/plugins.tsv

if [ ${#failed[@]} -gt 0 ]; then
  echo "Validation failed: ${failed[*]}"
  exit 1
fi
echo "All validators passed."
