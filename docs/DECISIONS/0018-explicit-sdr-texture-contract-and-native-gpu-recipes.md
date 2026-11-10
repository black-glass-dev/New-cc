# ADR 0018 — explicit SDR texture contract and C++ GPU recipes

Date: 2026-10-23. Status: accepted for candidate implementation; **not live integration**.

## Context
Phase 1C starts shared color-managed preview/export. Current playback is MediaPlayer;
export is Transformer 1.8.1. Replacing playback/lifecycle and enabling grade export in
one untested change risks regressions and silently divergent looks. Interface prose
alone is insufficient: pinned Media3 GlEffect says SDR textures are linear BT709, but
DefaultVideoFrameProcessor initializes DEFAULT (BT709 + SMPTE 170M transfer). Its LINEAR
mode also warns about SDR 8-bit banding. sRGB CPU input is not this video working texture.

## Decision
C++ owns explicit texture profiles, validation, original GLSL generation, immutable
baked parameters and CPU oracle. Profiles: sRGB, normalized linear Rec709, and explicitly
**Media3 1.8.1 DEFAULT SDR Rec709**. No implicit auto profile, YUV, HDR, Rec2020 or
premultiplied texture. Native recipe validation runs before GL allocation, even for
bypass. Identity/bypass/zero strength samples unchanged; alpha unchanged in all paths.
The Media3 SDR profile matches pinned decode breakpoint **0.0812**, encode **0.018**,
slope **4.5**, constants **1.099 / 0.099**, exponent **0.45**; do not replace with gamma
2.2, sRGB or a differently rounded BT709 transfer. Negative intermediates use the same
sign-preserving extension as the CPU reference. Clamp at normalized SDR output attachment,
not in the linear math. This is an SDR grading pass, not a general color management system.

Require full float32 fragment highp (precision >=23 bits, exponent ranges >=126/127).
CPU-valid grades can overflow floats; reject conservative worst-case intermediate bounds
above 1e25 before compilation, unless the native identity predicate eliminates processing.
This is an explicit GPU-supported subset, **not** a change to project/preset valid ranges.
Never silently clamp intermediate math, erase grades, or fall back to ungraded output.

GradedFramePipeline is an **uninstalled candidate** bundling effect + factory explicitly
set to WORKING_COLOR_SPACE_DEFAULT. It requires source ColorInfo BT709 + SDR + known
full/limited range; unknown/other-primary/PQ/HLG input refuses. Media3 owns decode/range
normalization; sampler receives a normalized 2D RGB709 texture, never a YUV/external OES
surface. HDR effect creation refuses too. GL creation/draw/release belong to caller's
GL thread, per-program resources and Media3 texture pool; native grade math stays out
of Kotlin. No mutable singleton GL state, timing change or per-frame JNI/math work.
No isNoOp shortcut trusts a separately supplied gradeActive flag; native-generated
identity shader defines behavior even for a manually constructed DTO.

## Evidence and remaining gates
Native six-suite/CTest, ASan+UBSan, strict JNI, real JVM and production pure Kotlin recipe
checks passed. Actual generated shaders ran on cache-only ANGLE/SwiftShader software
EGL/GLES: **96 profile/grade cases ×512 pixels**, maximum RGB error **1/255** vs native
double reference, exact alpha, neutral/bypass/zero-strength. Gate tolerance is <=2/255;
results sample vectors, not an all-settings proof. Twenty-three seeded grade recipes
supplement nine hand-picked cases; source chart includes ramps, color and alpha values.

This is **not** phone/hardware, Media3 frame conversion, playback, codec, encoded-export,
performance, or Android build evidence. GradeGpuContractTest adds Android synthetic
adapter tests, uncompiled/unrun here. Mesa CI host pass is configured, not yet verified.
**Keep active-grade render/remux guards.** No preview path replacement, effect insertion,
new UI, format bump or APK. Next: source metadata extraction/preflight, owned shared
preview/render connection, decoded rendered-frame comparisons, lifecycle/seek/cancel and
phone acceptance; only then remove guards for proven supported input.

## References and originality
- Pinned implementation: https://github.com/androidx/media/blob/1.8.1/libraries/effect/src/main/java/androidx/media3/effect/DefaultVideoFrameProcessor.java
- Interface discrepancy: https://github.com/androidx/media/blob/1.8.1/libraries/effect/src/main/java/androidx/media3/effect/GlEffect.java
- Transfer contract: https://github.com/androidx/media/blob/1.8.1/libraries/effect/src/main/assets/shaders/fragment_shader_transformation_sdr_internal_es2.glsl
- https://github.com/androidx/media/blob/1.8.1/libraries/effect/src/main/assets/shaders/fragment_shader_transformation_sdr_oetf_es2.glsl

Official Apache-2.0 sources inspected for public API/contract, not copied into the app.
Our operation graph, shaders and tests are original implementations of documented math.
Tool/driver provenance is recorded in DEPENDENCIES_AND_LICENSES; no driver shipped.
