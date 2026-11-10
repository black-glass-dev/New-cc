# API, execution ও shader ABI

## Host ownership / threading

`include/color/color_pipeline_api.hpp` primary C++ API; `bridge/color_c_api.h` version1 C ABI। `IColorEngine::backend_name()` বর্তমানে `CPUReference32F`। Engine object ও program/image assets caller-owned; `Image32F` move-only,64-byte alignment। Moved-from image/graph empty অবস্থায় থাকে। Pixel standard RGBA float layout,16 bytes; alpha explicitly fill করতে হবে (`Image32F` initially zero-filled)।

External `ColorFrameBuffer32F`-এ `row_stride_floats` ও `capacity_floats` বাধ্যতামূলক। Span validation: `(height-1)*stride + width*4 <= capacity`; size/address arithmetic overflow ও float alignment checked। Caller বাস্তব allocated capacity সত্যভাবে সরবরাহ করবে; library arbitrary invalid pointer বা fabricated capacity উদ্ধার করতে পারে না।

Render source ও destination dimensions একই হতে হবে; resize silently হয় না। Engine source snapshot এবং transactional destination-এর জন্য **দুটি full-frame scratch** রাখে; per-node full-frame image নয়। Frame scratch <= configured budget; default256MiB, tile64। Compiled graph metadata, caller-owned LUT/curve assets ও process overhead এই frame budget-এর বাইরে। Budget per call; unlimited parallel caller threads-এর combined RAM budget নয়। Tile traversal SIMD/GPU optimization-এর সমার্থক নয়।

Failure হলে caller destination untouched, including late-pixel NaN/overflow; padding untouched; input/output overlap supported। `RenderGradeFrame` throwing allocation/validation errorও destination commit করে না; free-standing pixel/spatial helpers-এর জন্য একই transactional contract দাবি করা হচ্ছে না। Scopes return owned counters; C output histogram only copied after successful complete scope computation।

Immutable program/assets/input নিয়ে একই engine-এর concurrent rendering supported; cache mutex-protected। Render চলার সময় graph বা asset mutate করবেন না; caller-এর app queue/ownership enforce করতে হবে। C handles একইভাবে caller-synchronized; JNI registry shared ownership + per-handle mutex দিয়ে close/render lifetime protect করে। JNI Java wrapper requires native-order, direct ByteBuffer; buffer position/limit নয়, full capacity ও explicit stride ব্যবহার হয়।

## Graph semantics

-0 node ID reserved; stage max256 nodes, max16 inputs/node। Missing edge/duplicate ID/cycle rejected। All nodes validated, including unused branches।
- No inputs = current stage input। One input = selected predecessor। By default last-added node output; `set_output(id)` explicit selection, `set_output(0)` restores default। Evaluation returns selected node, not an accidental last topological item।
- Serial applies operator. Parallel computes arithmetic mean of inputs, not vendor-specific add blend. LayerMixer composites input1 over input0, then input2, etc., straight-alpha. Outside takes `[base,alphaMatte]`, applies its own operator to base and blends by `1-clamp(matte.alpha)`। RGB Channel replicates selected channel; Combiner takes R from input0, G input1, B input2।
- Qualifier/window operators retain RGB and multiply alpha. They are mask-producing operations, not automatic RGB-key sockets. Use explicit composition।
- After node result: `key=key_input_gain*key_output_gain`, optionally `1-key`, then `mix(base,result,node_mix*key)`। Scalar key gains and mix are0..1; bypass returns base. Locked nodes cannot be replaced until explicitly unlocked।
- Shared fanout evaluates a DAG node once per pixel; shared-node/group/timeline **labels do not move nodes between graphs**। Actual order: `group_pre → clip → group_post → timeline`।
- Graph mutation uses globally unique monotonic epochs. Copies of unchanged graph share valid compiled plans; divergent edits get different epochs. Cache at most8 revision entries; immutable in-flight leases remain valid after eviction. No image cache or stale pixel key based only on frame/time exists।
- Context clip/group/source-revision are caller metadata, not a database lookup. Timeline time is validated but never changes input timing. Grain variation uses explicit `frame_index`; caller supplies the correct decoded/rendered frame index. No assumed24/30fps conversion।

## Shader ABI (source kernels, not an implemented dispatcher)

`shaders/common/{math,grade}.inc` এক original implementation GLSL450 এবং Metal3 source-এ included। `grade.comp` compiled to Vulkan1.1 SPIR-V in host tests. Metal compilation requires Apple compiler; portability is not asserted until that succeeds।

Grading buffers, binding/index:

| index | type | contents |
|---|---|---|
|0|readonly float4[]|input planes, contiguous N pixels per plane|
|1|writeonly float4[]|output N pixels|
|2|readonly float[]|operation-specific parameters|
|3|readonly float4[]|LUT/curve table, padded16-byte elements|
|4 Metal only|constant Meta|Vulkan uses push constants instead|

`Meta` field order: eight uint32 `count,width,op,frame,edge,curve_n,input_count,reserved`, then float32 `key_input,key_output,node_mix`, then uint32 `invert` — total48 bytes। Count divisible by width, positive dimensions; `frame` low32 frame index। Key/mix parameters valid0..1; shader host must validate extents, descriptor sizes, assets, enum values, domain/finite parameters and device limits **before dispatch**। Host dispatcher নিচে `IVulkanCompute` হিসেবে আছে (COLOR_BUILD_VULKAN=ON), bundled kernels-এর জন্য; DAG-to-GPU scheduler নেই।

|op|meaning|float parameter layout|
|---|---|---|
|0|CST|inputTransfer, outputTransfer, exposure, knee, compression, matrix9 row-major|
|1|Primaries|lift3,gamma3,gain3,offset3,exposure,contrast,pivot,saturation,hue,vibrance,temp,tint,lumaMix|
|2|Log|shadows3,midtones3,highlights3,low,high,softness|
|3|HDR|stops6,centers6,falloff,softClip|
|4|Slices|seven hue/sat/density triplets,width,shadowTint3,highlightTint3|
|5|Mixer|matrix9 row-major,monochromeFlag|
|6|HSL qualifier|center3,width3,softness3,invertFlag|
|7|Window|shape,cx,cy,sx,sy,rotation,feather,invertFlag|
|8|Curves|enabled flags master/red/green/blue; table float4 channels in same order|
|9|Tetra LUT|domainMin3,domainMax3; `edge` controls table indexing|
|10|Grain|amount,colored,seed **bitcast uint→float**, not numeric float conversion|
|11|Parallel|no params;input_count2..16|
|12|Layer|no params;input_count2..16|
|13|Outside|three planes:base,matte,already-graded base|
|14|Channel|selected channel0..2|
|15|Combiner|three planes:R-source,G-source,B-source|
|16|RAW RGB stage|black3,white3,whiteBalance3,exposure,matrix9 row-major|

CST matrix comes from `PreparedCST::matrix()`; double elements must be explicitly packed as float row-major. LUT values from `Lut3D::values()` are **12-byte Vec3**, not directly bindable as float4; pad/pack them and use domain getters। Curves bake each enabled curve at the same sample count; absent channels may contain identity samples with enable flag0. Minimum2 samples; common4096 is an engineering choice, not universal1e-5 interpolation guarantee for arbitrary curves। LUT data is red-fastest, no texture sRGB filtering।

Layer/outside/parallel/matte kernels cover composition math, and SwiftShader execution checks them. Full CPU graph→GPU scheduling, dirty-tile barriers and multi-clip resource reuse remain **not implemented**. CPU failure-atomicity is not a claim about these raw kernels. All GPU buffers must be valid, non-overlapping where required, zero-initialized for atomic counters, and synchronized by a future backend।

## Halation / scope kernels

Halation Meta: uint32 width,height,radius,phase, float threshold,amount. Radius1..32. Phase0 writes horizontal thresholded-luma blur; phase1 reads that plus source and writes warm bloom. Buffers0 source float4,1 horizontal float,2 output float4,3 output-horizontal float,4 Metal metadata. A real memory barrier is required between passes।

Scopes Meta: uint32 count,width,bins,columns;float low,high;uint32 transfer,reserved —32 bytes। Buffers0 pixels,1 atomic uint counters,2 XYZ row-major float9,3 Metal metadata. Counters layout: histogram4*B, parade3*W, waveformW, vectorscopeG, CIE-xyG, undefined-chromaticity1; W=B*columns,G=B*B. Zero before dispatch. GPU counters32-bit, CPU counters64-bit; GPU count, products/indices and accumulation must fit uint32. GPU scopes are source code, not GPU-executed results in this environment।

## Bridges

C ABI protects exception boundary and provides thread-local last error until next protected call; opaque handles must not be forged/double-freed. C API currently exposes serial exposure/CST addition, rendering and histogram extraction; C++ API exposes the larger operator set. Program mutation remains caller-synchronized।

`android_vulkan_jni.cpp` historical integration filename notwithstanding is explicitly a **CPU JNI** implementation; it does not create a Vulkan device. `ColorEngine.java` matching wrapper lives in this module, not existing app. `ios_metal_bridge.mm` genuinely creates a pipeline state from supplied compiled Metal library; command encoding, buffer dispatch, synchronization and readback are not implemented।

## Added in final batch

- `include/color/extended_tools.hpp`: analysis, Bayer demosaic, spatial filters, polygon window. Spatial ops run as materialized DAG passes with bounded live-frame pool.
- `include/color/tracking.hpp`: Shi–Tomasi features, pyramidal KLT, affine RANSAC, affine sizing. Affine model is not projective/homography.
- `include/color/grade_archive.hpp`: `.ocg` binary archive (magic `OCLRGRD1`, length + FNV checksum, bounded counts, no code payloads); `LookGallery` stores validated keys atomically.
- `include/color/mastering.hpp`: independent pixel outputs from one timeline program. Not metadata authoring or certified mastering.
- `include/color/vulkan_compute.hpp` / `src/vulkan_compute.cpp`: runtime-loaded Vulkan 1.1 compute utility. Checks SPIR-V magic, descriptor count, storage budget, push-constant size, device limits, uses explicit upload/readback and host synchronization. No CPU fallback.
- Metal `ColorDispatchMetal` in `bridge/ios_metal_bridge.mm`: synchronous shared-buffer dispatch. Not compiled/executed here.
- C ABI: `ColorProgramAddSpatial`, `ColorProgramSerialize`, `ColorProgramDeserialize`.

## GPU DAG scheduler (`GpuGradeEngine`)
Host-staged: each node uploads inputs, dispatches `grade.comp` or `spatial.comp` through `IVulkanCompute`, and reads back synchronously. Output blend uses the same `mix(base,result,mix*key)` rule as the CPU path. The Outside matte, ExternalPlane, and spatial-inside-Outside cases have explicit handling: the last one throws. There is no device-side pooling, no zero-copy surface, and no phone-tuned scheduling.
