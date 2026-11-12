# ADR0036 — Validate visible decoder geometry, not padded allocation dimensions

Date:2026-11-03. Candidate implemented; Android/device validation pending.

## New phone evidence
User reports no wheel live preview, Play or experimental preview in edaabbc. Shared failure:
`MediaCodecVideoRenderer ... video/hevc hvc1.2.4.H150.B0 [2880,2160,24.0,
ColorInfo(BT709, Limited range, SDR SMPTE170M, false,10bit Luma,10bit Chroma)]`
→ `decoded video geometry differs from admitted input`. Media3 says format_supported=YES.
This is declared input information, not measured decoded dimensions or proof of10-bit output.
No raw decoder crop/width/height or source-probe geometry appears in the supplied exception.
Do not assert a confirmed codec/alignment/rotation root cause from that truncated context.

## Official research
- Android MediaCodec docs: raw dimensions can include a cropped picture; right/bottom are
  inclusive, visible width=right-left+1, height=bottom-top+1. Crop is before rotation; Surface
  output applies transforms. https://developer.android.com/reference/android/media/MediaCodec
- Inspected pinned official Media3 1.8.1 MediaCodecVideoRenderer.onOutputFormatChanged: uses
  all four crop keys to determine visible width/height, then swaps reported VideoSize for
  rotation90/270 because decoder Surface already applies it. Source:
  https://github.com/androidx/media/blob/1.8.1/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/video/MediaCodecVideoRenderer.java
- Inspected getCodecInfo protected API for diagnostics. Existing pinned1.8.1 remains; no
  dependency/license upgrade, decoder replacement, private reflection or downloaded code.

## Defect / correction
Our raw MediaFormat adapter ignored crop while the renderer used it. Exact raw allocation
comparison can therefore reject a legitimately padded output. This code defect is confirmed;
its occurrence on the actual user's file remains unmeasured. Both wheel inline and static
experimental preview use the shared GradedPreviewRenderers and get this correction.

New C++20 visible_video_geometry owns validation/calculation; narrow JNI uses7 fixed ints
(coded width,height,presence mask,left,top,right,bottom), returns two dimensions. Empty crop
mask leaves dimensions unchanged. All four keys are required for crop; partial/unknown masks,
nonpositive sizes, negative/reversed/out-of-bounds crop fail closed. Inclusive arithmetic is
bounds-checked up to INT_MAX. Kotlin transports metadata, no grading/domain math. Source
MediaExtractor geometry and actual decoder MediaFormat geometry use the same native helper.

The gate now compares normalized **pre-rotation visible pixels**. No hardcoded HEVC alignment,
blind height rounding, equal-pixel-count heuristic, SAR scaling or width/height swapping.
Stride/slice-height are diagnostics only, not picture size. Existing strict native comparison
still rejects changed visible dimensions, unknown/HDR/changed color and null decoder output
on each rendered buffer. Malformed normalization runs inside terminal gate handling.
Media3 still owns crop/rotation/Surface transforms; this does not manually crop/resample pixels.
The Transformer candidate adapter exposes Format, not raw crop coordinates; its strict gate
is not silently relaxed. Normal graded timeline/export remains guarded as before.

## Diagnostics and disclosure
Native mismatch messages contain expected-visible and actual-visible WxH. Renderer wraps
failures with stage(input-track/decoder-format/decoded-buffer/graph-input/video-sink-setup),
codec name, source expected geometry/rotation, input geometry/rotation/pixel ratio, source
and raw decoder crop/stride/slice/color keys. No URI, CSD/HDR byte dump or provider path.
Source probe retains original geometry description in ephemeral metadata only (no project
schema/persistence change). Full selectable Details keeps1200 chars per cause,6 causes;
brief diagnostics retain300. Missing/invalid diagnostic keys are explicit, never guessed.
No automatic Rec709/HDR/bit-depth inference or new Input override is introduced.

## Verification / acceptance
Make6 native suites and343 bilingual/3theme/17label checks PASS. Strict JNI build + real
pinned Kotlin/JVM/JNI tests PASS crop padding, partial masks, bad bounds, INT_MAX arithmetic,
malformed JNI schema; terminal crop/changed-visible tests added. Fixtures include HEVC-shaped
2880x2176→2880x2160 via explicit crop, not a measured user decoder report. Android adapter
regression source checks crop normalization, rotation/stride independence and changed-crop
refusal; compilation/execution pending. Need Android build/lint and phone replay/play/back-
ground/Before/source/HDR validation. No device, pixel-transform, exact source-file,10-bit
output or export-parity evidence. Readiness remains25% complete/75% remaining.
