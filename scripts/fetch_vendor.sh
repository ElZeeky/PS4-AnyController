#!/usr/bin/env bash
# Clones the third-party reference sources this project builds on.
# They are NOT vendored into git (see .gitignore) so we always know we're
# reading a specific, reviewable revision.
#
# Usage: scripts/fetch_vendor.sh
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p vendor
cd vendor

clone_pinned() {
  local url="$1" dir="$2" rev="$3"
  if [ -d "$dir/.git" ]; then
    echo "== $dir already present, leaving as-is"
    return
  fi
  echo "== cloning $url -> $dir"
  git clone "$url" "$dir"
  ( cd "$dir" && git checkout --detach "$rev" )
}

# crucifix86/xbox_controller_plugin (MIT) - the reference implementation whose
# Xbox HID -> OrbisPadData mapping we must preserve. Credit is kept in NOTICE.
clone_pinned https://github.com/crucifix86/xbox_controller_plugin.git \
  xbox_controller_plugin "$(cat ../third_party/REFS 2>/dev/null | awk -F= '/^xbox_controller_plugin=/{print $2}')"

# OpenOrbis PS4 toolchain - crt, link script, stub libs and headers.
clone_pinned https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain.git \
  OpenOrbis-PS4-Toolchain "$(cat ../third_party/REFS 2>/dev/null | awk -F= '/^OpenOrbis-PS4-Toolchain=/{print $2}')"

# ps4-payload-sdk (GPL-3.0) - the CRT sources and linker script a GoldHEN payload
# needs. `./build.sh payload` looks for it at .tools/ps4-payload-sdk unless
# PS4_PAYLOAD_SDK points somewhere else, and clones it on first use. Only the
# sources are needed: the build compiles them with Zig, so `make` is not run.
clone_pinned https://github.com/ps4-payload-dev/sdk.git \
  ps4-payload-sdk "$(cat ../third_party/REFS 2>/dev/null | awk -F= '/^ps4-payload-sdk=/{print $2}')"

echo "done"
