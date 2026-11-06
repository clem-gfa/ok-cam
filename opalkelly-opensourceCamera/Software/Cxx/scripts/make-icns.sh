#!/usr/bin/env bash
# Copyright (c) 2026 Opal Kelly Incorporated
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# make-icns.sh <source-png> <output-icns>
# Build a macOS .icns app icon from a single square PNG, using the base-system sips + iconutil (no
# extra tooling). Sizes are derived by scaling the source, so any reasonably large square PNG works.
set -uo pipefail   # best-effort: a missing icon must not fail the build

src="$1"
out="$2"
[ -f "$src" ] || { echo "make-icns: source $src not found; skipping icon"; exit 0; }
command -v iconutil >/dev/null 2>&1 || { echo "make-icns: iconutil not available; skipping icon"; exit 0; }

set="$(mktemp -d)/icon.iconset"
mkdir -p "$set"
for s in 16 32 128 256 512; do
    sips -z "$s" "$s" "$src" --out "$set/icon_${s}x${s}.png" >/dev/null 2>&1 || true
    d=$((s * 2))
    sips -z "$d" "$d" "$src" --out "$set/icon_${s}x${s}@2x.png" >/dev/null 2>&1 || true
done
iconutil -c icns "$set" -o "$out" && echo "make-icns: wrote $out"
