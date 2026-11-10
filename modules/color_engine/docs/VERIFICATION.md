# Executed verification — 2026-10-18 (UTC)

এটি host reference foundation ও software Vulkan parity-এর report; সব requested phase সম্পূর্ণ হওয়ার প্রমাণ নয়।

## Commands (repo root)
```sh
build_color/tools/cmake/data/bin/cmake -S modules/color_engine -B build_color \
  -DCOLOR_GLSLANG="$PWD/build_color/tools/bin/glslang" -DCOLOR_REQUIRE_SHADER_VALIDATION=ON \
  -DCOLOR_BUILD_VULKAN=ON -DCOLOR_VULKAN_HEADERS="$PWD/build_color/tools/build/vulkan-headers/include"
build_color/tools/cmake/data/bin/cmake --build build_color -j 2
LD_LIBRARY_PATH="$PWD/build_color/tools/build/swiftshader" \
VK_ICD_FILENAMES="$PWD/build_color/tools/build/swiftshader/vk_swiftshader_icd.json" \
  build_color/tools/cmake/data/bin/ctest --test-dir build_color --output-on-failure
```

## Results
| Check | Result |
|---|---|
| Standalone Debug build, GCC12.2, C++20, `-Werror` warnings | PASS |
| CTest (12 entries) | **12/12 PASS** |
| `color_tests` reference suite | **88,967 assertions PASS** |
| `color_extended_tests` (analysis, Bayer RAW, spatial, tracking, archive, mastering, DAG, Lut1D, rounded window, depth matte, reframe, ExternalPlane) | **6,325 assertions PASS** |
| `color_gpu_parity` on SwiftShader Vulkan 1.1 device | **6,417 executed GPU-vs-CPU assertions PASS** (1e-5 operator tolerance; 2e-4 for the `GpuGradeEngine` DAG: Primaries+Halation, ExternalPlane, and a steep curve; includes rounded SDF window and op 22 Lut1D) |
| `color_vulkan_readback` (8 chained grade dispatches) | PASS |
| Actual glslang 16.6.0 → Vulkan 1.1 SPIR-V, 7 GLSL kernels | PASS (grade, halation, scopes, spatial, demosaic, resize, tracking) |
| C ABI caller test (create, render, scopes, errors, CST, serialize/deserialize, spatial node) | PASS |
| ASan + UBSan + float-cast-overflow + leak detection, all 12 CTest entries | **12/12 PASS**, 0 sanitizer reports (log: `build_color/verification-asan.log`) |
| `color_bench` (host CPU reference timing, 1920x1080, 3 runs) | Runs; **host-only numbers, no threshold** |
| Release parent `add_subdirectory` consumer, linked and executed | PASS (backend `CPUReference32F`, output within expected range) |
| Metal shader compile (`validate_metal.sh`) | **UNRUN**, exit 77: Apple `xcrun`/Metal not available |
| Metal runtime dispatch (`ColorDispatchMetal`) | **Not compiled/executed** |
| Android NDK JNI + Java wrapper | **Not compiled** here (no NDK) |
| Apple ObjC++ bridge compile | **Not compiled** here (no Apple toolchain) |
| Hardware GPU (any physical Vulkan GPU), device timing, thermal, 4K sustained playback, codec/export | **Not run** |

## Important scope limits of these tests
- SwiftShader is a CPU-based software Vulkan implementation. It validates SPIR-V/descriptor/push-constant/dispatch/readback logic, **not** a phone or desktop GPU's performance or driver behaviour.
- The Vulkan dispatcher is a bounded utility for the bundled kernels. It is not a complete DAG-to-GPU scheduler.
- The 1e-5 tolerance is for the bundled reference fixtures and does not generalize to arbitrary media or arbitrary curve/LUT inputs.
- Analytic/CPU reference tests verify engineering math and invariants; they do not certify perceptual, ACES, DRT, Dolby or Resolve-equivalent output.

## Coverage notes
- 12 transfer functions: sampled round trips, named knees, normalized PQ/HLG domains, DI/ACEScct anchors.
- 11 gamuts: paired conversions through Bradford, singular/nonfinite rejection.
- Operator reference tests: primaries, log, HDR, slices, mixer, HSL, windows, curves, LUT, grain, halation, CST, RAW, polygon, composition, scopes.
- LUT: 1,000 nonlinear coordinates checked against independent tetrahedral and trilinear references; strict `.cube` syntax tests.
- Spatial: bilateral noise reduction, unsharp, morphology, feathering, temporal denoise, dehaze, halation, aliasing/atomicity.
- Tracking: Shi–Tomasi, pyramidal KLT on synthetic translation, flat-patch rejection, RANSAC affine recovery with outliers.
- Curve bake: a steep curve (slope ~1000) has max lookup error 8.2e-3 at 4096 samples, 6.5e-4 at 16384, and 4.1e-5 at 65536. The engine picks the smallest size under 1e-4 and throws if none qualifies.
- Lut1D: 3-entry `.cube` parse, interpolation, malformed/NaN rejection.
- Rounded-rectangle window: corner inside plain rectangle but outside rounded SDF; radius validation. Same op on GPU vs CPU.
- Depth hook `depth_matte`: band mask, inversion, NaN rejection. `reframe_transform`: Fit/Fill/Stretch mapping.
- ExternalPlane: CPU materialized path dimension check; GPU graph agrees with CPU and rejects mismatches.
- Archive: all operator types round-trip canonically; truncation/integrity/version rejection; atomic disk gallery.

## Artifacts
Logs and generated reports under `build_color/` (ignored by Git). Large downloaded tool sources and sanitizer intermediates were removed after verification. Tool binaries are kept only under `build_color/tools/`.

## Toolchain note (this batch)
The `build_color/` directory was lost between sessions. It was rebuilt with `modules/color_engine/tests/bootstrap_tools.sh`: cmake 3.31.6 (PyPI), glslang 16.6.0 (GitHub source), Vulkan-Headers v1.3.296, and the SwiftShader ICD from `@sparticuz/chromium` 131.0.1 (npm). Those are host validation tools only. Earlier logs, including `build_color/verification/` and `build_color/gpu-parity.log`, were not restored, so the results above come from this session's rerun.

## Update: curve sample-count fix
The first run of the new curve reference found a real defect: a fixed 4096-sample GPU curve table exceeded the 2e-4 contract on steep curves. `GpuGradeEngine` now chooses 4096, 16384, or 65536 samples per program. Re-run results are in the table above (ASan log re-generated).
