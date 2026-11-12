#!/usr/bin/env bash
# Builds a minimal LGPL-only FFmpeg for Android. No --enable-gpl, no external codec libraries.
# Enabled: file protocol, mov/matroska/mp4 muxers, mov/matroska demuxers, h264/hevc parsers.
# Used for stream-copy remux of the platform-encoded MP4 into MOV or Matroska.
#
# Usage: scripts/build_ffmpeg_android.sh <ndk-dir> <out-dir> [abi ...]
# Output: <out-dir>/<abi>/{include,lib}  (static libraries, PIC, linked into owncut_android)
set -euo pipefail

FFMPEG_TAG="n8.1.3"
FFMPEG_SHA256="09f990289327d3ebfedf4fc15cae3f884a65dab7a5aac7bbb5b01ff83900efd9"
API_LEVEL=26

NDK="${1:?usage: $0 <ndk-dir> <out-dir> [abi ...]}"
OUT="${2:?usage: $0 <ndk-dir> <out-dir> [abi ...]}"
shift 2
ABIS=("$@")
if [[ ${#ABIS[@]} -eq 0 ]]; then ABIS=(arm64-v8a); fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ARCHIVE="$WORK/ffmpeg-${FFMPEG_TAG}.tar.gz"
curl -sSL --retry 3 -o "$ARCHIVE" "https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/${FFMPEG_TAG}"
echo "${FFMPEG_SHA256}  ${ARCHIVE}" | sha256sum -c -
tar -xzf "$ARCHIVE" -C "$WORK"
SRC="$WORK/FFmpeg-${FFMPEG_TAG}"
test -x "$SRC/configure"

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
  PREFIX="$OUT/$ABI"
  mkdir -p "$BUILD" "$PREFIX"

  # x86_64 assembly needs nasm; disabled to keep the build self-contained.
  EXTRA=()
  if [[ "$ARCH" == "x86_64" ]]; then EXTRA+=(--disable-x86asm); fi

  (
    cd "$BUILD"
    "$SRC/configure" \
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
      --extra-cflags="-fPIC" \
      --disable-everything \
      --disable-programs \
      --disable-doc \
      --disable-debug \
      --disable-network \
      --disable-autodetect \
      --disable-pkg-config \
      --disable-avdevice \
      --disable-avfilter \
      --disable-swscale \
      --disable-swresample \
      --disable-postproc \
      --disable-shared \
      --enable-static \
      --enable-pic \
      --enable-avformat \
      --enable-avcodec \
      --enable-avutil \
      --enable-protocol=file \
      --enable-demuxer=mov,matroska \
      --enable-muxer=mov,matroska,mp4 \
      --enable-parser=h264,hevc \
      "${EXTRA[@]}"
    make -j"$(nproc)"
    make install
  )

  test -f "$PREFIX/lib/libavformat.a"
  test -f "$PREFIX/lib/libavcodec.a"
  test -f "$PREFIX/lib/libavutil.a"
  printf '%s %s\n' "$FFMPEG_TAG" "$FFMPEG_SHA256" > "$PREFIX/FFMPEG_VERSION"
  echo "built FFmpeg ${FFMPEG_TAG} for ${ABI} -> ${PREFIX}"
done
