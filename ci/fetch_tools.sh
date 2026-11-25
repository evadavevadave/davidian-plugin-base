#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Downloads the plugin validators used by ci/validate.sh into build/tools/, at pinned
# releases. They run on the build machines only; nothing from them goes into a release.
#   ci/fetch_tools.sh <linux|macos|windows>
#
#   pluginval       Tracktion, GPL-3.0     VST3 and AU
#   clap-validator  free-audio, MIT        CLAP
# lv2lint has no binary releases; the Linux CI job builds it from source (see build.yml).
set -euo pipefail

os="$1"
PLUGINVAL_TAG="v1.0.4"
CLAP_VALIDATOR_TAG="0.4.1"
tools="build/tools"
mkdir -p "$tools"

# URL of the asset of release <tag> of <repo> whose name matches <pattern> (grep -E).
asset_url() {
  local auth=()
  if [ -n "${GITHUB_TOKEN:-}" ]; then auth=(-H "Authorization: Bearer $GITHUB_TOKEN"); fi
  curl -fsSL "${auth[@]}" "https://api.github.com/repos/$1/releases/tags/$2" \
    | grep -o '"browser_download_url": *"[^"]*"' | sed 's/.*: *"//; s/"$//' \
    | grep -E "$3" | head -n 1
}

unpack() { # zip -> dir
  if command -v unzip > /dev/null 2>&1; then unzip -q -o "$1" -d "$2"; else 7z x -y -o"$2" "$1" > /dev/null; fi
}

case "$os" in
  linux)   pv="pluginval_Linux.zip";   cv="-ubuntu-22\.04\.zip$" ;;
  macos)   pv="pluginval_macOS.zip";   cv="-macos-universal\.zip$" ;;
  windows) pv="pluginval_Windows.zip"; cv="-windows\.zip$" ;;
  *) echo "usage: ci/fetch_tools.sh <linux|macos|windows>"; exit 2 ;;
esac

url=$(asset_url Tracktion/pluginval "$PLUGINVAL_TAG" "/$pv$")
curl -fsSL -o "$tools/pluginval.zip" "$url"
unpack "$tools/pluginval.zip" "$tools/pluginval"

url=$(asset_url free-audio/clap-validator "$CLAP_VALIDATOR_TAG" "$cv")
curl -fsSL -o "$tools/clap-validator.zip" "$url"
unpack "$tools/clap-validator.zip" "$tools/clap-validator"
# Linux and macOS releases wrap a .tar.gz; the binary is at its root or in binaries/.
for t in "$tools"/clap-validator/*.tar.gz; do
  if [ -e "$t" ]; then tar -xzf "$t" -C "$tools/clap-validator"; fi
done
if [ -f "$tools/clap-validator/binaries/clap-validator" ]; then
  cp "$tools/clap-validator/binaries/clap-validator" "$tools/clap-validator/"
fi
chmod +x "$tools/clap-validator/clap-validator"* "$tools/pluginval/pluginval" 2> /dev/null || true

echo "pluginval $PLUGINVAL_TAG and clap-validator $CLAP_VALIDATOR_TAG in $tools:"
ls -l "$tools/pluginval" "$tools/clap-validator"
