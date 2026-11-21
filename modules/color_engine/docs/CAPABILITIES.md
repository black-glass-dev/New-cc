# বাস্তব capability ও অসম্পূর্ণ কাজ

এই ফাইল শুধু পরীক্ষিত/কোড-বিদ্যমান কাজ লেখে। "কোড আছে" আর "সব ফোনে/সব GPU-তে যাচাইকৃত" আলাদা।

## Phase 1 — science / float infrastructure
**Implemented + tested:** RGBA32F aligned ownership, capacity/stride/overflow validation, tiled CPU execution; Rec.709/2020, P3-D65, AP0/AP1, DWG, AWG3/4, S-Gamut3/3.Cine, V-Gamut; 12 transfer curves (Linear, sRGB, Gamma2.4, Rec.709, DI, ACEScct, PQ, HLG, LogC3 EI800, LogC4, S-Log3, V-Log); Bradford; prepared CST; analytic highlight knee/chroma compression; sensor RGB black/white/WB/exposure/matrix parameters।
**Implemented:** Bayer 16-bit demosaic (RGGB/BGGR/GRBG/GBRG, bilinear, strided input); auto exposure/grey-world WB; statistical shot-match gain/offset (variance + mean matching).
**Not complete:** camera-container/RAW file decoder (CRW/CR2/NEF/ARW/BRAW/R3D ইত্যাদি), vendor SDK integration, highlight reconstruction, SIMD intrinsics, pooled tile allocator, zero-copy GPU surfaces, mobile memory/thermal tuning। RAW parameters শুধু already-demosaiced/Bayer-codes-এর color stage; vendor-accurate RAW processing নয়।

## Phase 2 — graph / execution
**Implemented + tested:** cycle/missing-edge detection, max256 nodes/stage, max16 edges/node, explicit output, serial pass, arithmetic-mean parallel mixer, straight-alpha layer mixer, outside matte, RGB split/combine, fanout result reuse, scalar key/mix, locks/bypass, layer-list projection, four ordered grade stages (`group_pre → clip → group_post → timeline`), bounded compiled-graph cache, copy/fork/move-safe revisions।
**Newly added:** spatial nodes (bilateral, unsharp, matte erode/dilate/feather, temporal denoise, dehaze, halation) via materialized pass with bounded live-frame pool; polygon window node; matte union/intersection/subtraction nodes; serialized grade archive (`.ocg`) with integrity check।
**Limits:** `SharedNode`, group, timeline kinds are serial-compatible metadata tags; no cross-clip shared-grade registry; caller selects a program; context IDs do not resolve a hidden clip database; rendered pixels are not cached; no arbitrary blend modes, temporal dependency graph or dirty-tile incremental scheduling।

## Phase 3 — grading / tools
**Implemented CPU + corresponding GLSL/MSL kernels (GLSL executed on SwiftShader, see VERIFICATION):** CST; primaries; DI-weighted log wheels; six HDR exposure zones; seven hue-density slices; RGB mixer; HSL qualifier; circle/rectangle/linear window; polygon window; cubic curves + baked 1D lookup; tetrahedral 3D LUT; deterministic grain; halation (two-pass); spatial filters; scopes (histogram/parade/waveform/vectorscope/CIE)।
**Implemented, CPU only (no GPU kernel path for every item):** pixel-space affine planar tracking, Shi–Tomasi detection, pyramidal Lucas–Kanade KLT, robust affine RANSAC, affine sizing (bilinear), original `.cube` 3D LUT parser (strict, 2–65 edge), analysis/auto/shot-match helpers, multi-master render (SDR/HDR as independent pixel outputs), touch-wheel controller।
**Not implemented:** advanced auto-grade/shot-match from reference clips with learned models, gamut/HDR perceptual warpers, advanced curve families, Bezier/Boolean window geometry beyond polygon/circle/rect, matte morphology beyond erode/dilate/blur, object or AI tracking, AI magic mask, depth estimation, deflicker, sharpening/restoration suite, DCTL compiler/sandbox, full film emulation, PowerGrade/gallery UI, reframing/sizing beyond affine, multi-master metadata/Dolby/HDR10+ mastering, 1D/shaper LUT।

Analytic tone knee কোনো ACES RRT/ODT, vendor DRT বা certified HDR mastering transform নয়।

## Phase 4 — bridges / verification
**Executed:** standalone GCC12.2 C++20/C build with strict warnings; 3 host test binaries + 12 CTest entries pass; actual glslang16.6.0 → Vulkan1.1 SPIR-V of 7 GLSL kernels; **Vulkan compute dispatcher (dlopen loader) executed on SwiftShader software Vulkan 1.1 device**: 5244 GPU↔CPU parity assertions; ASan/UBSan/float-cast-overflow; Release parent `add_subdirectory` consumer run; C ABI test with serialization/spatial/render calls।
**Source present but unverified here:** Android NDK JNI/Java wrapper compile and runtime; Apple ObjC++ Metal dispatch (`ColorDispatchMetal`) and Metal source compile (script exit 77, no Xcode)।
**Not executed:** hardware GPU (any real Vulkan/Adreno/Mali/Apple GPU), device timing, sustained 4K playback, thermal behaviour, codec/export integration, Android/iOS device validation, production GPU scheduling of the whole DAG।
`create_engine()` সর্বদা explicit `CPUReference32F`; GPU backend আলাদা API দিয়ে ব্যবহৃত হয় এবং কোনো automatic fallback নেই।

## সিদ্ধান্ত
পরীক্ষিত reference foundation এবং একটি software Vulkan parity path আছে। পুরো requested inventory, hardware GPU performance, device/mobile readiness, Resolve feature parity, certification কিংবা production release সম্পন্ন হয়নি।

## এই ব্যাচে যোগ হওয়া কাজ (host-verified, device-unverified)
- **Lut1D** (`.cube` `LUT_1D_SIZE`, grade op 22): CPU parser/apply, GLSL op 22, Metal source mirror (Metal compile UNRUN). CPU ও SwiftShader GPU parity আছে।
- **Rounded-rectangle window** (`PowerWindow::Shape::RoundedRectangle`, `corner_radius`, op 7 shape 3): CPU, GLSL, Metal source; GPU parity আছে।
- **ExternalPlane** (অন্য image/depth plane matte হিসেবে): CPU materialized path ও `GpuGradeEngine` দুটোতেই dimension-checked। `.ocg` archive এ লেখা যায় না, ইচ্ছাকৃতভাবে।
- **depth_matte()**: depth map থেকে smooth band alpha matte। এটি একটি helper; কোনো depth-estimation model নয়। AI depth নেই।
- **reframe_transform()**: Fit/Fill/Stretch affine helper। Auto-reframe tracking নয়।
- **GpuGradeEngine** (`gpu_graph.hpp`): Primaries + Halation DAG SwiftShader-এ 2e-4 tolerance-এ CPU-এর সাথে মেলে; ExternalPlane match করে; unsupported combination throw করে, CPU fallback নেই। Curve table এখন 4096/16384/65536 sample-এর মধ্যে বাছাই হয়; steep curve-এর জন্য 4096 যথেষ্ট ছিল না (max error 8.2e-3), তাই 1e-4 পর্যন্ত নিশ্চিত করা হয়। কোনো size যথেষ্ট না হলে error দিয়ে বন্ধ হয়।
- **color_bench**: host CPU timing harness। কোনো pass/fail threshold নেই এবং এটি phone/GPU/thermal ফলাফল নয়।

**এখনও হয়নি / ব্যবহারকারীর পরীক্ষা বাকি:** hardware GPU, phone performance, thermal, 4K playback, codec/export; Metal runtime ও Apple compile; Android JNI compile। RAW camera decoder, AI/object tracking, DCTL, film emulation, restoration, full auto-grade, Dolby/HDR10+ metadata—এগুলো deferred, implement করা হয়নি। Resolve equivalence বা certification দাবি নয়।

## মোবাইল ও RAW বাদে বাকি কাজের অবস্থা
- **Curve table accuracy:** ঠিক করা ও test যুক্ত হয়েছে (CPU reference ও GPU DAG উভয়েই)।
- **Known cost, not fixed:** spatial pass-এ input plane কপি হয় কারণ `ComputeBuffer` non-const span নেয়। এটি performance cost, correctness issue নয়।
- **Still not done (ইচ্ছাকৃতভাবে বা environment-এর কারণে):** Metal compile ও runtime (xcrun নেই); Android JNI compile (NDK নেই) এবং Apple bridge compile—এগুলো mobile পর্যায়ের, তাই আপনার নির্দেশ অনুযায়ী এখানে ধরা হয়নি। RAW camera decoder ও RAW video editing বাদ। AI/object tracking, DCTL, film emulation, restoration, full auto-grade, Dolby/HDR10+ আগের সিদ্ধান্ত অনুযায়ী deferred।
