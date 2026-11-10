# Own Color Engine — isolated C++20 reference core

এটি একটি original color-science ও grading foundation। **পূর্ণ Resolve-equivalent engine নয়; চারটি phase-এর সম্পূর্ণ feature inventory এখনও শেষ হয়নি।** কার্যকর CPU implementation, genuine shader source, executed tests এবং অসম্পূর্ণ/device-unverified অংশ আলাদা রাখা হয়েছে। কোনো proprietary implementation বা Graden/GPL port অন্তর্ভুক্ত করা হয়নি।

সমস্ত source এই directory-তে; generated tools/build outputs শুধু repository-এর `build_color/`-তে। root app, timeline engine বা root CMake-এ integration পরিবর্তন করা হয়নি।

## Build / test

Repository root থেকে, CMake 3.20+ এবং C++20 compiler:

```sh
cmake -S modules/color_engine -B build_color -DCMAKE_BUILD_TYPE=Debug
cmake --build build_color -j 2
ctest --test-dir build_color --output-on-failure
```

এই session-এর local CMake binary: `build_color/tools/cmake/data/bin/cmake`। GLSL validation বাধ্যতামূলক করতে:

```sh
cmake -S modules/color_engine -B build_color \
  -DCOLOR_GLSLANG="$PWD/build_color/tools/bin/glslang" \
  -DCOLOR_REQUIRE_SHADER_VALIDATION=ON
cmake --build build_color -j 2
ctest --test-dir build_color --output-on-failure
```

`COLOR_GLSLANG` ছাড়া default build শুধু host tests চালায়; সেটিকে shader validation বলা যাবে না। এটি glslang/glslangValidator executable-এর path; shell script/text scanner নয়। আলাদা `spirv-val` validation এই session-এ চালানো হয়নি।

Sanitizers: `-DCOLOR_SANITIZE=ON`, build directory `build_color/asan`। Apple compiler validation script: `modules/color_engine/tests/validate_metal.sh`; Xcode না থাকলে explicit exit **77/UNRUN**।

Parent integration:

```cmake
set(COLOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/color_engine)
target_link_libraries(your_target PRIVATE color::engine)
```

Android/iOS-specific bridge targets কেবল সংশ্লিষ্ট toolchain-এ তৈরি হয়। Host build এই targets যাচাই করে না।

## ছোট C++ integration

```cpp
#include <color/color_pipeline_api.hpp>
using namespace color;
auto engine = create_engine(); // backend_name() == "CPUReference32F"
Image32F source(1920,1080), graded(1920,1080);
// Caller fills source.pixels() with valid straight-alpha RGBA32F.
GradeProgram program;
Node node; node.id=1; node.label="Exposure";
Primaries grade; grade.exposure=.5F;
node.operation=grade;
program.clip.add(node);
engine->RenderGradeFrame(program,source.view(),graded.view(),
                        GradeContext{42,7,1,0.0,0});
auto scopes=engine->ComputeVideoScopes(graded.view(),ScopeConfig{});
```

Input transfer/gamut অনুমান করা হয় না। Camera log decode করতে explicit CST node, সঠিক gamut ও transfer দিন। PQ/HLG ব্যবহারের আগে [science contract](docs/SCIENCE.md) পড়ুন।

## কী পাওয়া যাচ্ছে

- 64-byte aligned RGBA32F image ownership, checked stride/capacity, bounded per-call frame scratch, tiled traversal, failure-atomic output commit ও in-place rendering।
- 11 gamut matrices, Bradford adaptation, 12 transfer curves এবং prepared CST।
- DAG topological validation, explicit output selection, graph revision cache, serial/parallel/straight-alpha layer/outside/RGB split-combine, key/mix/lock/bypass ও mobile layer projection।
- Primaries, log/HDR zones, seven hue slices, channel mixer, HSL key, circle/rectangle/gradient windows, shape-preserving cubic curves, tetrahedral/trilinear `.cube` LUT, deterministic grain ও separable halation।
- Already-demosaiced RAW RGB color-stage parameters; touch-wheel node-edit controller।
- Histogram/waveform/parade/vectorscope/CIE-xy CPU scopes; GLSL/Metal grading, compositing, halation ও scope kernels।
- Exception-safe C ABI; Android CPU JNI ownership bridge; Apple Metal pipeline-state creation helper। **GPU render dispatcher নয়।**

[Capability/remaining-work matrix](docs/CAPABILITIES.md) · [API/shader ABI](docs/ARCHITECTURE.md) · [executed verification](docs/VERIFICATION.md) · [science references](docs/SCIENCE.md)

## Optional software-Vulkan parity (host)

```sh
cmake -S modules/color_engine -B build_color -DCOLOR_BUILD_VULKAN=ON -DCOLOR_VULKAN_HEADERS=/path/to/Vulkan-Headers/include -DCOLOR_GLSLANG=/path/to/glslang
```

এই test একটি software Vulkan ICD (যেমন SwiftShader) দরকার। ICD না থাকলে test `UNRUN` (exit 77) হিসেবে skip হয়; hardware GPU performance বা device validation এর প্রমাণ নয়।
