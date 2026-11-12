#!/usr/bin/env bash
# Builds kvazaar (BSD-3-Clause HEVC encoder) with 8-bit and 10-bit sample storage into ONE static
# library, so FFmpeg can offer software HEVC Main and Main 10 encoders on any phone.
#
# kvazaar selects its sample width at compile time (KVZ_BIT_DEPTH), so the 10-bit objects are
# renamed: every kvz_* symbol becomes kvz10_*, and the object files get a 10_ prefix.
#
# Usage: scripts/build_kvazaar.sh <kvazaar-source-dir> <output-prefix> [extra cmake args...]
# Environment: OBJCOPY (default objcopy; the NDK's llvm-objcopy for Android), CMAKE (default cmake).
# Output: <prefix>/lib/libkvazaar.a, <prefix>/include/kvazaar.h, <prefix>/lib/pkgconfig/kvazaar.pc
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <kvazaar-source-dir> <output-prefix> [cmake args...]" >&2
  exit 2
fi
SRC="$(cd "$1" && pwd)"
OUT="$2"
shift 2
EXTRA=("$@")
OBJCOPY="${OBJCOPY:-objcopy}"
CMAKE="${CMAKE:-cmake}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

common=(-DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5)

"$CMAKE" -S "$SRC" -B "$WORK/b8" "${common[@]}" "${EXTRA[@]}" -DCMAKE_C_FLAGS="-DKVZ_BIT_DEPTH=8" > "$WORK/cfg8.log" 2>&1 \
  || { tail -n 60 "$WORK/cfg8.log"; exit 1; }
"$CMAKE" --build "$WORK/b8" -j"$(nproc)" --target kvazaar > "$WORK/build8.log" 2>&1 \
  || { tail -n 60 "$WORK/build8.log"; exit 1; }

"$CMAKE" -S "$SRC" -B "$WORK/b10" "${common[@]}" "${EXTRA[@]}" -DCMAKE_C_FLAGS="-DKVZ_BIT_DEPTH=10" > "$WORK/cfg10.log" 2>&1 \
  || { tail -n 60 "$WORK/cfg10.log"; exit 1; }
"$CMAKE" --build "$WORK/b10" -j"$(nproc)" --target kvazaar > "$WORK/build10.log" 2>&1 \
  || { tail -n 60 "$WORK/build10.log"; exit 1; }

LIB8="$WORK/b8/libkvazaar.a"
LIB10="$WORK/b10/libkvazaar.a"
test -f "$LIB8"
test -f "$LIB10"

# Rename every global kvz_* symbol of the 10-bit build (and the references inside it).
nm -g --defined-only "$LIB10" | awk 'NF==3 && $3 ~ /^kvz_/ {print $3, "kvz10_" substr($3, 5)}' | sort -u > "$WORK/rename.txt"
test -s "$WORK/rename.txt"
mkdir -p "$WORK/o8" "$WORK/o10"
(cd "$WORK/o8" && ar x "$LIB8")
(cd "$WORK/o10" && ar x "$LIB10")
for object in "$WORK"/o10/*.o; do
  "$OBJCOPY" --redefine-syms="$WORK/rename.txt" "$object"
done
mkdir -p "$WORK/merged"
for object in "$WORK"/o8/*.o; do cp "$object" "$WORK/merged/$(basename "$object")"; done
for object in "$WORK"/o10/*.o; do cp "$object" "$WORK/merged/10_$(basename "$object")"; done
rm -f "$WORK/libkvazaar.a"
ar rcs "$WORK/libkvazaar.a" "$WORK"/merged/*.o
ranlib "$WORK/libkvazaar.a" 2>/dev/null || true

mkdir -p "$OUT/lib" "$OUT/include" "$OUT/lib/pkgconfig"
cp "$WORK/libkvazaar.a" "$OUT/lib/libkvazaar.a"
cp "$SRC/src/kvazaar.h" "$OUT/include/kvazaar.h"
VERSION="$(sed -n 's/^VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt" | head -n 1)"
cat > "$OUT/lib/pkgconfig/kvazaar.pc" <<EOF
prefix=$OUT
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: libkvazaar
Description: kvazaar HEVC encoder (8-bit and 10-bit, BSD-3-Clause)
Version: ${VERSION:-2.3.1}
Libs: -L\${libdir} -lkvazaar
Libs.private: -lm -lpthread
Cflags: -I\${includedir}
EOF
echo "built kvazaar 8-bit + 10-bit -> $OUT"
