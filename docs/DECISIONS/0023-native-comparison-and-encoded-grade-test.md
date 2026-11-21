# ADR0023 — native frame diagnostics and real encoded-grade test source

2026-10-24. Native/host diagnostics verified; Android encode test UNCOMPILED/UNRUN.

## Contract
C++ compare_rgba8 validates both views, equal dimensions, extents/strides and64Mi-pixel
budget, then compares active RGBA8 bytes without writing input/padding. Aliasing/readonly
views are allowed. Return pixel count, each RGB channel's maximum absolute error, overall
mean absolute RGB error, signed mean(actual−expected) and alpha-mismatched pixel count.
64-bit sums are bounded; diagnostics are code-value distances, NOT DeltaE, temporal
alignment, calibration, HDR, transfer conversion or proof two frames depict the same time.

Stateless nativeCompareRgba8V1 direct-buffer JNI returns7 doubles (count,maxRGB,meanAbs,
meanSigned,alphaCount). Counts are exact within the bounded budget. Kotlin immutable DTO
and ARGB transport expose metrics without duplicating comparison math. Existing still
render packing refactors into the same unchanged RGBA transport; no grade math changes.

## Actual encoder test source, not composition assertions
GrayVideoFixture authors a constant neutral RGB signal(.4,.4,.4) through its own EGL2
recordable encoder surface, with AVC/Rec709/limited/SDR settings and muxer tags. It does
NOT assign guessed tags to imported unknown media. Surface input avoids vendor-specific
YUV input-image layout assumptions. Output is30 frames at30fps,128x128, explicit timestamps,
EOS drain and owned GL/surface/codec/muxer cleanup. It needs a real device encoder/EGL.
Driver interpretation remains unmeasured; configured metadata is not calibration proof.

GradedExportParityTest creates two native clips of that source. Actual candidate Transformer
runner encodes neutral, +1/−1stop segments and parameter-preserving bypass to three separate
scratch outputs. Check actual output duration; retrieve six source-time frames (including
950ms/1050ms around1s cut), require sRGB ARGB8888, sample a32x32 center region. Grade the
decoded neutral sample with native CPU oracle, compare decoded graded sample using native
metrics. Also require positive/negative brightness changes in the correct segments and
bypass close to neutral, so grade dropping/wrong clip assignment cannot pass via tolerance.
Shipping grade guards are asserted intact; fixture/output caches are removed on exit.

Proposed lossy8-bit gates: reference mean<=8,max per-channel<=16, exact alpha; bypass
mean<=3,max<=8; signed grade change>12 first segment/<−12 second. These are **unvalidated
initial gates**, NOT observed performance/phone results or the shader <=2/255 threshold.
Log all per-time metrics; investigate failures before changing thresholds. No skips or
fallback to ungraded export. Codec normalization/tag refusal may fail before any output.

Constant-region sampling avoids proxy resize-order/edge differences but does NOT cover
full-frame color/chroma/gradient/gamut/HDR, actual presentation timing, A/V sync or live
preview pixels. A neutral encode-based oracle is a diagnostic round-trip, not independent
physical calibration. Separate real live-display/export/geometry/device tests remain.

## Evidence
Passed locally: Make6, Release CTest6/6, color/comparison ASan+UBSan, full strict production
JNI build; actual JVM readonly/alias/invalid-buffer comparison and production Kotlin2.3.21
-Werror exact metrics(channel max/mean/bias/alpha), pixel/source/bypass/draft/gate/plan/
projection/progress regressions;298bilingual strings/3themes/17labels,7metadata/3patch tests.
Native comparison cases cover stride/padding, alias, dimension/short-span/zero refusal.

Neither Android test was compiled/run, nor was EGL/fixture/codec candidate actually executed.
No new GPU run, measured codec tolerance, live-display parity, new APK or install-over proof.
No shipping guard removal/new readiness credit: phase1C remains incomplete,25%/75%.
No new runtime/build dependency or persisted format change; original math/test code only.
