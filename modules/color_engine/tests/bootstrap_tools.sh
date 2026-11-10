#!/bin/sh
# Reproducible host tooling for validation only. Everything is written under build_color/tools.
# Downloads: PyPI (cmake, Brotli), GitHub codeload (glslang, Vulkan-Headers), npm registry (SwiftShader ICD).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TOOLS="$ROOT/build_color/tools"
BUILD="$TOOLS/build"
mkdir -p "$TOOLS/bin" "$TOOLS/licenses" "$BUILD"
export PIP_CACHE_DIR="$ROOT/build_color/pip-cache"
CMAKE="$TOOLS/cmake/data/bin/cmake"
if [ ! -x "$CMAKE" ]; then
 python3 -m pip install --target "$TOOLS" 'cmake==3.31.6'
fi
if [ ! -x "$TOOLS/bin/glslang" ]; then
 curl -fL --retry 2 https://codeload.github.com/KhronosGroup/glslang/tar.gz/refs/tags/16.6.0 -o "$BUILD/glslang-16.6.0.tar.gz"
 mkdir -p "$BUILD/glslang-source" && tar -xzf "$BUILD/glslang-16.6.0.tar.gz" --strip-components=1 -C "$BUILD/glslang-source"
 "$CMAKE" -S "$BUILD/glslang-source" -B "$BUILD/glslang" -DENABLE_OPT=OFF -DENABLE_HLSL=OFF -DGLSLANG_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
 "$CMAKE" --build "$BUILD/glslang" -j 2
 cp "$BUILD/glslang/StandAlone/glslang" "$TOOLS/bin/glslang"
 cp "$BUILD/glslang-source/LICENSE.txt" "$TOOLS/licenses/glslang-LICENSE.txt"
 rm -rf "$BUILD/glslang" "$BUILD/glslang-source" "$BUILD/glslang-16.6.0.tar.gz"
fi
if [ ! -d "$BUILD/vulkan-headers/include/vulkan" ]; then
 mkdir -p "$BUILD/vulkan-headers"
 curl -fL --retry 2 https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.3.296 -o "$BUILD/vulkan-headers.tar.gz"
 tar -xzf "$BUILD/vulkan-headers.tar.gz" --strip-components=1 -C "$BUILD/vulkan-headers" && rm -f "$BUILD/vulkan-headers.tar.gz"
 cp "$BUILD/vulkan-headers/LICENSE.md" "$TOOLS/licenses/Vulkan-Headers-LICENSE.md" 2>/dev/null || true
fi
if [ ! -f "$BUILD/swiftshader/vk_swiftshader_icd.json" ]; then
 # Software Vulkan ICD for validation of SPIR-V/dispatch logic only; not a hardware GPU.
 mkdir -p "$BUILD/swiftshader" "$BUILD/brotli"
 python3 -m pip install --target "$BUILD/brotli" 'Brotli==1.1.0' >/dev/null
 curl -fL --retry 2 https://registry.npmjs.org/@sparticuz/chromium/-/chromium-131.0.1.tgz -o "$BUILD/chromium.tgz"
 tar -xzf "$BUILD/chromium.tgz" -C "$BUILD" package/bin/swiftshader.tar.br package/LICENSE
 cp "$BUILD/package/LICENSE" "$TOOLS/licenses/chromium-swiftshader-LICENSE.txt"
 PYTHONPATH="$BUILD/brotli" python3 -c "import brotli,tarfile,io,sys; t=tarfile.open(fileobj=io.BytesIO(brotli.decompress(open(sys.argv[1],'rb').read()))); t.extractall(sys.argv[2])" "$BUILD/package/bin/swiftshader.tar.br" "$BUILD/swiftshader"
 rm -rf "$BUILD/package" "$BUILD/chromium.tgz" "$BUILD/brotli"
fi
"$CMAKE" --version | head -1
"$TOOLS/bin/glslang" --version | head -1
