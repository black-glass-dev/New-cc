#!/bin/sh
# Run on macOS with Xcode. No textual imitation of a Metal compiler is accepted.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="$ROOT/build_color/metal"
command -v xcrun >/dev/null 2>&1 || { echo 'UNRUN: Apple xcrun/Metal compiler required' >&2; exit 77; }
mkdir -p "$OUT"
for shader in grade halation scopes spatial demosaic tracking resize; do
 xcrun -sdk macosx metal -std=metal3.0 -c "$ROOT/modules/color_engine/shaders/metal/$shader.metal" -o "$OUT/$shader.air"
done
xcrun -sdk macosx metallib "$OUT/grade.air" "$OUT/halation.air" "$OUT/scopes.air" "$OUT/spatial.air" "$OUT/demosaic.air" "$OUT/tracking.air" "$OUT/resize.air" -o "$OUT/color.metallib"
echo 'PASS: actual Apple Metal compilation (does not prove runtime parity)'
