# Color grading reference pipeline v1

## Original wheels and HDR-placeholder mitigation (2026-10-25)

Color editor now uses four native-mapped wheels plus Primary/Channels drag values; no visual
sliders/SeekBars. C++ owns RGB-plane mapping/bounds/master preservation, Kotlin pointer/artwork.
Absent/exact25-byte zero HDR padding is unset; nonzero/malformed data remains blocked, with
PQ/HLG/unknown source checks intact. Possible false-presence fix, not confirmed phone cause.
User reports4K10-bit; still preview remains8-bit bounded proxy, not10-bit pipeline/export proof.
No decompiled/proprietary material, new dependency, export guard or readiness change (ADR0032).

Actual local native color tests and331resource/3theme/17label checks pass. Added JNI/UI test
source pending execution/build. New APK and user-phone acceptance pending;25%/75%.


## Frame diagnostics / encoded-test source (2026-10-24)

Native compare_rgba8 measures byte-domain per-channel maximum, overall mean absolute/
signed RGB(actual−expected) error and alpha mismatch. It validates equal size/strides/
spans, permits readonly/alias and ignores padding, without conversion/alignment/mutation.
These are not perceptual/HDR/calibration/timestamp metrics. Host/JNI/Kotlin checks pass.

Device test source authors constant gray SDR, encodes neutral/graded/bypass candidate
clips, decodes sRGB sample regions and compares against native grade of neutral output.
This diagnostic oracle is lossy/constant-region, not an independent all-color or live
preview calibration. Android execution and proposed gates remain unverified (ADR0023).


## Isolated experimental GPU clip preview (2026-10-24)

Color action captures saved clip in shared ColorRenderPlan, then renderer-owned video graph
uses the same native shader and DEFAULT SDR factory as candidate export. Native raw-format
checks run at output changes and before rendered buffers; unknown/changed output refuses.
Before removes only the grade effect during playback. Loop stays in trims, no timestamp/
model edits. Native gate/host tests pass; Android decode/GL/color/audio/thread/crop/rotation
and exported pixels remain unverified. Existing still proxy is not a parity oracle; normal
playback/export unchanged, no full1C claim (ADR0022).


## Actual still preview / candidate encoder entry (2026-10-23)

ClipColorEditor probes admitted source, retrieves source-time sRGB ARGB8888 bitmap, scales
a <=1280x720 proxy and calls native RGBA8 grading through ColorFrameBuffers. Before/After
switches actual bitmaps; successfully rendered current draft authorizes one Apply command.
No grading math in Kotlin. Preview scale-before-grade differs from candidate video's grade-
before-Presentation; do not claim encoded parity or live playback. Unknown/HDR source or
non-sRGB bitmap refuses. exportGradedCandidate can invoke real codec worker for developer
validation, but is unrun and not a production route; guards remain (ADR0021).


## Decoded output gate (2026-10-23; candidate only)

Native DecodedVideoFormat compares admitted expected vs current decoder output, with
positive exact coded dimensions and matching known SDR color/range, no HDR metadata.
Missing/changed tags refuse instead of reusing probe defaults. DecoderColorGate validates
every render via metadata JNI (cost unmeasured), latching failures/close. No per-frame
shader generation or full-pixel JNI is introduced. GuardedDecoderFactory checks both surface release
overloads before forwarding and leaves timestamps/audio unchanged. Candidate.configure
binds it via DefaultAssetLoaderFactory; no live route uses it. Source identity, crop/
rotation, actual codec reporting, pixels and threading remain separate unverified gates;
strict geometry/range checks may reject valid normalization. Native/JNI/Kotlin tests pass,
Android adapter/tests unrun; prior software shader result is not codec proof (ADR0020).


## Source admission and shared plan (2026-10-23; candidate)

C++ now admits only explicit Rec709/SDR-video/full-or-limited with no HDR-static metadata;
unknown/sRGB/PQ/HLG/other tags refuse. Platform code maps constants into canonical JNI
codes, never guesses transfer from resolution or Media3 defaults. Worker VideoSourceProbe
reads raw local-file/content formats, requires one video track and releases in finally.
Blocking provider calls are not interruptible; cancellation is checked around them.

ColorRenderPlan snapshots native grade values and recipes with detached source metadata;
per-request URI dedup only, immutable publication/no partial plan. Candidate composition
places grade per clip before global720p Presentation and binds an explicit processor
factory. No per-frame JNI. Current preview/export do not call it; guards remain. Future
live decoder must revalidate format and handle source changes; metadata is not byte
identity/calibration/codec proof. ColorInfo-only overload is synthetic adapter helper,
not proof raw tags were present. Native/JNI/pure-Kotlin tests pass; Android unrun (ADR0019).


## Phase 1C shared GPU foundation (2026-10-23; unconnected candidate)

The original sRGB CPU RGBA8 contract below is unchanged. New color_gpu.hpp adds explicit
normalized straight-alpha Rec709 texture profiles: sRGB, linear709, and **Media3 1.8.1
DEFAULT SDR709** (SMPTE170M-encoded working texture). texture_reference decodes the
chosen profile, calls the same native linear pipeline, then encodes; input RGB must be
finite [0,1]. Shader output can exceed [0,1]; normalized SDR output attachments clamp,
matching CPU reference packing. No raw YUV/OES/HDR/Rec2020/premultiplied support implied.

Native shader recipes bake validated immutable Grade values, require full float32 fragment
highp and conservatively reject unsafe intermediate bounds above 1e25 (except native
identity/bypass/zero-strength shortcuts). Project-valid settings are unchanged. Same
linear operation order/alpha, no UI/Kotlin math, no frame timestamps modified. sRGB uses
the existing standard constants. Media3 SDR decode threshold is **0.0812**, encode 0.018,
slope4.5, 1.099/0.099, exponent0.45; constants intentionally match pinned effect processor,
not gamma2.2 or sRGB. Transfers use sign-preserving negative intermediate extension.

Candidate GradedFramePipeline pairs an explicit DEFAULT factory with its effect; its
source gate requires known BT709 SDR full/limited ColorInfo and rejects HDR. **Do not
insert the effect with an unrelated/LINEAR/ORIGINAL factory.** Media3 owns source decode/
range normalization; this adapter receives an already normalized 2D RGB texture. Input
metadata extraction/provenance and actual caller integration remain pending. Resources
are per GL program/Media3 pool, created/drawn/released on the owning GL thread.

Actual software GLES shader/reference run: **96 recipes/profiles ×512 pixels**, RGB max
**1/255**, exact alpha/identity/bypass/zero strength. Includes 23 seeded recipe variants
and nine hand-picked cases per profile. No Android adapter compilation, Media3 processor
conversion, decoded video/export, phone or performance proof. Keep active-grade export
guards; current preview is ungraded. ADR 0018 and PROGRESS record evidence/limitations.


## Phase 1B update — clip/project binding (2026-10-23)

Clip now owns a Grade and ProjectCodec v2 embeds the fixed preset payload for each
clip. The standalone codec contract below is unchanged, but it is also the bounded
project grade representation. Native grouped history/reset/paste/bypass, edit inheritance,
v0/v1 neutral migration and recovery are implemented/host-tested (ADR 0017). Undo is
session-local, not persisted. Exact native settings/activity flow through immutable
snapshots. ColorEngine remains a frame adapter; no current live preview/export effect.
Active grades are guarded from ungraded export until phase 1C parity is established.

2026-10-23: implementation/host-tested, not enabled in Android preview/export.

## Contract

Original C++20 module `engine/color`, standard library only. No proprietary source,
assets, LUTs or editor-specific algorithms copied. Domain math stays native; Kotlin
ColorGrade contains immutable values and ColorEngine exposes a stateless frame call.

Explicit input: **sRGB-encoded straight-alpha RGBA8**, Rec.709/sRGB RGB primaries/D65.
This is not YUV, BT.709 video-transfer decode, arbitrary Android bitmap layout, linear
RGBA, premultiplied alpha or HDR. A future video adapter must inspect/convert color
metadata, not assume all video uses sRGB transfer. Transfer constants follow standard
sRGB; negative values use a documented sign-preserving extension. This is not an
ICC/ACES/OCIO engine or a calibrated Kelvin white-balance implementation.

## Original operation order

For each channel, decode encoded sRGB with the standard 0.04045/12.92 breakpoint,
2.4 exponent and 0.055/1.055 constants. Working channels are linear-light:

1. `x = input * 2^(exposure_stops + channel_balance_stops)`.
2. `x = (x + lift * (1 - x)) * gain`.
3. `x = sign(x) * abs(x)^(1 / gamma)`.
4. `x = pivot + (x - pivot) * contrast`.
5. `Y = 0.2126 R + 0.7152 G + 0.0722 B`;
   `saturated = Y + saturation * (channel - Y)`.
6. `result = original + strength * (saturated - original)` in linear light.
7. Encode sRGB using linear breakpoint 0.0031308 and reciprocal 2.4 exponent.
   Clamp only when packing SDR bytes; round half-up to nearest 8-bit integer.

RGB balance is per-channel stops, not temperature/tint or chromatic adaptation.
Identity/bypass/zero strength has an exact active-byte copy path. Alpha is copied
without change, including zero alpha; hidden RGB can be graded in non-identity mode.
Floating reference `Pipeline::linear` retains negative/highlight values and bounds
input to +/-65504; this alone is not HDR support. The CPU reference is not presented
as a realtime 4K solution. GPU parity and performance are still acceptance work.

## Settings and buffer safety

See color.hpp for all finite ranges: exposure [-8,8], contrast/saturation [0,4],
pivot [0.001,1], strength [0,1], channel balance [-2,2], lift [-1,1], gamma [0.1,4],
gain [0,4]. Invalid values are rejected even when bypassed, never silently clamped.

Dimensions must be positive, total pixels <= 64*1024*1024, stride >= width*4 and spans
cover every active row without arithmetic overflow. This safety budget is not a device
resolution/performance claim. Source/output extents must not overlap, even in place.
All structural/settings checks precede output writes. Input and padding never change.
Optional native cancellation is cooperative: false means discard a possibly partial
destination. No transactional output guarantee on cancellation; source remains intact.

JNI nativeRenderV1 uses a fixed 17-double block: exposure, contrast, saturation, pivot,
strength, RGB balance, RGB lift, RGB gamma, RGB gain; enabled is separate. Buffers must
be direct byte buffers, position=0, limit=capacity, output writable. Use full-capacity
slices for regions; native validates these conditions and overlap. Calls belong on a
worker; JNI does not schedule or insert grading into the current player/Transformer.

## Preset format

Standalone preset, **not** project persistence. `OWNCUTCG` (8 bytes), uint32 LE version
1, byte enabled (0/1), then 17 IEEE-754 binary64 LE values in the field order above:
**149 bytes total**. No struct padding, trailing bytes, NaN/Inf or unsupported versions.
Malformed/truncated input is rejected; unsupported versions use UnsupportedGradeVersion.
Phase 1B now embeds this payload in project/autosave v2; old v0/v1 migrate neutral.
The standalone preset remains v1. See ADR 0017 for project binding and history.

## Histogram

Native RGB channels are encoded 8-bit bins. Luminance bins encode linear Rec.709
luminance back to sRGB. By default fully transparent pixels are excluded; other alpha
values count once, not alpha-weighted. This is not a broadcast waveform, vectorscope,
calibrated nits display or HDR scope.

## Evidence and remaining acceptance

Unit tests: transfer vectors, all 17 parameter ranges/nonfinite rejection, controls,
exact identity bytes, known +1-stop pixel vector (128 -> 176), alpha/source/padding,
stride/size/overlap/overflow defenses, cancellation, row-tile equivalence, histograms,
preset round trips/truncation/future versions and randomized extreme settings.

Real JVM -Xcheck:jni tests native buffer handling. Additionally app-pinned Kotlin 2.3.21
compiles the **production ColorEngine.kt** and calls production JNI/C++ on a host JVM,
checking real pixels, 17-field order, bypass, read-only/alias/invalid-buffer rejection.
This is stronger than fixture-only JNI tests but **not Android/ART/GPU/codec testing**.

Remaining acceptance: Android managed persistence/recovery, color metadata,
shared preview/export GPU adapter, live controls/before-after, curves/LUT/secondary tools,
scopes UI and actual Android build/device/encoded-video parity. No grading APK delivered.
