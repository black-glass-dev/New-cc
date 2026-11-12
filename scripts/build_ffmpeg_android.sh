#!/usr/bin/env bash
# Builds a minimal LGPL-only FFmpeg for Android. No --enable-gpl.
# External library: kvazaar (BSD-3-Clause HEVC encoder), built per ABI with 8-bit and 10-bit
# sample storage (scripts/build_kvazaar.sh). FFmpeg's libkvazaar wrapper is patched for yuv420p10le
# (scripts/patch_ffmpeg_kvazaar.py).
# Enabled: file protocol, mov/matroska/mxf/mp4 muxers, mov/matroska demuxers, h264/hevc parsers,
# AAC and H.264 decoders, prores_ks, dnxhd and libkvazaar encoders, libswscale, libswresample and
# the h264_mp4toannexb bitstream filter.
# Used for stream-copy remux of the platform-encoded MP4 into MOV, Matroska or MXF
# (MXF audio is AAC decoded and resampled to 48 kHz PCM), for ProRes 422 HQ or DNxHR HQ
# transcodes of an H.264 MP4 export, and for software HEVC (8-bit and 10-bit) on devices without a
# hardware HEVC encoder.
#
# Usage: scripts/build_ffmpeg_android.sh <ndk-dir> <out-dir> [abi ...]
# Output: <out-dir>/<abi>/{include,lib}  (static libraries, PIC, linked into owncut_android)
set -euo pipefail

FFMPEG_TAG="n8.1.3"
FFMPEG_SHA256="09f990289327d3ebfedf4fc15cae3f884a65dab7a5aac7bbb5b01ff83900efd9"
KVAZAAR_TAG="v2.3.1"
KVAZAAR_SHA256="c5a1699d0bd50bc6bdba485b3438a5681a43d7b2c4fd6311a144740bfa59c9cc"
API_LEVEL=26
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

NDK="${1:?usage: $0 <ndk-dir> <out-dir> [abi ...]}"
OUT="${2:?usage: $0 <ndk-dir> <out-dir> [abi ...]}"
shift 2
ABIS=("$@")
if [[ ${#ABIS[@]} -eq 0 ]]; then ABIS=(arm64-v8a); fi

WORK="$(mktemp -d)"
CURRENT_BUILD=""
# On failure, surface the tail of configure/make logs as a CI annotation (readable through the
# checks API when raw job logs are unavailable) and as the step summary.
report_failure() {
  local status=$?
  local tail_text=""
  for log in "$CURRENT_BUILD/ffbuild/config.log" "$CURRENT_BUILD/configure.log" "$CURRENT_BUILD/build.log" "$CURRENT_BUILD/install.log"; do
    if [[ -n "$CURRENT_BUILD" && -f "$log" ]]; then
      tail_text+="== ${log##*/} ==\n$(tail -n 40 "$log")\n"
    fi
  done
  # kvazaar logs (written before configure) so that a kvazaar failure is visible in the annotation.
  for log in "$WORK"/kvazaar-*.log; do
    if [[ -f "$log" ]]; then
      tail_text+="== ${log##*/} =="$'\n'"$(tail -n 60 "$log")"$'\n'
    fi
  done
  tail_text="${tail_text:-no build log (failed before configure: exit ${status})}"
  echo "$tail_text" | tail -c 6000
  local encoded="${tail_text//%/%25}"
  encoded="${encoded//$'\r'/%0D}"
  encoded="${encoded//$'\n'/%0A}"
  echo "::error title=FFmpeg Android build failed::${encoded}"
  if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    { echo '### FFmpeg Android build failed'; echo '```'; echo "$tail_text" | tail -c 6000; echo '```'; } >> "$GITHUB_STEP_SUMMARY"
  fi
  rm -rf "$WORK"
  exit "$status"
}
trap report_failure ERR
trap 'rm -rf "$WORK"' EXIT

ARCHIVE="$WORK/ffmpeg-${FFMPEG_TAG}.tar.gz"
curl -sSL --retry 3 -o "$ARCHIVE" "https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/${FFMPEG_TAG}"
echo "${FFMPEG_SHA256}  ${ARCHIVE}" | sha256sum -c -
tar -xzf "$ARCHIVE" -C "$WORK"
SRC="$WORK/FFmpeg-${FFMPEG_TAG}"
test -x "$SRC/configure"

KVZ_ARCHIVE="$WORK/kvazaar-${KVAZAAR_TAG}.tar.gz"
curl -sSL --retry 3 -o "$KVZ_ARCHIVE" "https://codeload.github.com/ultravideo/kvazaar/tar.gz/refs/tags/${KVAZAAR_TAG}"
echo "${KVAZAAR_SHA256}  ${KVZ_ARCHIVE}" | sha256sum -c -
tar -xzf "$KVZ_ARCHIVE" -C "$WORK"
KVZ_SRC="$WORK/kvazaar-${KVAZAAR_TAG#v}"
test -f "$KVZ_SRC/CMakeLists.txt"
python3 "$SCRIPT_DIR/patch_ffmpeg_kvazaar.py" "$SRC"

HOST_TAG="linux-x86_64"
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG"
SYSROOT="$TOOLCHAIN/sysroot"
test -d "$SYSROOT"

for ABI in "${ABIS[@]}"; do
  case "$ABI" in
    arm64-v8a) TRIPLE="aarch64-linux-android"; ARCH="aarch64" ;;
    x86_64)    TRIPLE="x86_64-linux-android";  ARCH="x86_64" ;;
    *) echo "unsupported ABI: $ABI" >&2; exit 2 ;;
  esac

  BUILD="$WORK/build-$ABI"
  CURRENT_BUILD="$BUILD"
  PREFIX="$OUT/$ABI"
  mkdir -p "$BUILD" "$PREFIX"

  # Assembly is disabled for every ABI. x86_64 needs nasm, and the aarch64 NEON transform tables
  # (ff_tx_tab_*) are referenced without PIC, which fails to link into owncut_android.so.
  # The remux needs only AAC decode and resampling, which run fine in C.
  EXTRA=(--disable-asm)

  # kvazaar: 8-bit and 10-bit HEVC in one static archive, built with the NDK toolchain.
  KVZ_PREFIX="$WORK/kvazaar-$ABI"
  OBJCOPY="$TOOLCHAIN/bin/llvm-objcopy" "$SCRIPT_DIR/build_kvazaar.sh" "$KVZ_SRC" "$KVZ_PREFIX" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-${API_LEVEL}" \
    -DLINUX=1 -DCMAKE_HAVE_LIBC_PTHREAD=1 -DCMAKE_HAVE_THREADS_LIBRARY=1 -DCMAKE_USE_PTHREADS_INIT=1 -DCMAKE_THREAD_LIBS_INIT= \
    > "$WORK/kvazaar-$ABI.log" 2>&1 \
    || { tail -n 80 "$WORK/kvazaar-$ABI.log"; false; }
  test -f "$KVZ_PREFIX/lib/libkvazaar.a"

  (
    cd "$BUILD"
    PKG_CONFIG_PATH="${KVZ_PREFIX}/lib/pkgconfig" "$SRC/configure" \
      --prefix="$PREFIX" \
      --target-os=android \
      --arch="$ARCH" \
      --enable-cross-compile \
      --cc="$TOOLCHAIN/bin/${TRIPLE}${API_LEVEL}-clang" \
      --cxx="$TOOLCHAIN/bin/${TRIPLE}${API_LEVEL}-clang++" \
      --ar="$TOOLCHAIN/bin/llvm-ar" \
      --ranlib="$TOOLCHAIN/bin/llvm-ranlib" \
      --strip="$TOOLCHAIN/bin/llvm-strip" \
      --nm="$TOOLCHAIN/bin/llvm-nm" \
      --sysroot="$SYSROOT" \
      --extra-cflags="-fPIC -I${KVZ_PREFIX}/include" \
      --extra-libs="-lm"  # bionic provides pthread in libc \
      --disable-everything \
      --disable-programs \
      --disable-doc \
      --disable-debug \
      --disable-network \
      --disable-autodetect \
      --disable-avdevice \
      --disable-avfilter \
      --enable-swscale \
      --enable-swresample \
      --disable-shared \
      --enable-static \
      --enable-pic \
      --enable-avformat \
      --enable-avcodec \
      --enable-avutil \
      --enable-protocol=file \
      --enable-demuxer=mov,matroska \
      --enable-muxer=mov,matroska,mp4,mxf \
      --enable-parser=h264,hevc \
      --enable-decoder=aac,h264 \
      --enable-encoder=prores_ks,dnxhd,libkvazaar \
      --enable-libkvazaar \
      --enable-bsf=h264_mp4toannexb \
      "${EXTRA[@]}" > configure.log 2>&1 || { tail -n 80 configure.log; false; }
    make -j"$(nproc)" > build.log 2>&1 || { tail -n 80 build.log; false; }
    make install > install.log 2>&1
    cp "$KVZ_PREFIX/lib/libkvazaar.a" "$PREFIX/lib/libkvazaar.a"
  )

  test -f "$PREFIX/lib/libavformat.a"
  test -f "$PREFIX/lib/libavcodec.a"
  test -f "$PREFIX/lib/libavutil.a"
  test -f "$PREFIX/lib/libswresample.a"
  test -f "$PREFIX/lib/libswscale.a"
  test -f "$PREFIX/lib/libkvazaar.a"
  printf '%s %s\nkvazaar %s %s\n' "$FFMPEG_TAG" "$FFMPEG_SHA256" "$KVAZAAR_TAG" "$KVAZAAR_SHA256" > "$PREFIX/FFMPEG_VERSION"
  echo "built FFmpeg ${FFMPEG_TAG} for ${ABI} -> ${PREFIX}"
done
