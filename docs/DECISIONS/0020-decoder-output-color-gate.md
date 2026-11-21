# ADR 0020 — candidate decoder output color gate

2026-10-23. Accepted for candidate source; Android/codec integration unverified.

## Why
Probing a URI and constructing a shader is not decoded-format validation. Decoder
output may omit/change tags, range or geometry. A one-time successful output check
must not authorize subsequent changed output. Current shipping grading guards remain.

## Contract
C++ validates expected/actual DecodedVideoFormat: both must pass ADR0019 admission;
positive coded width/height must match exactly, as must color primaries/transfer/range.
HDR-static data and unknown output tags refuse. This intentionally strict initial subset
also rejects potentially legitimate padding/adaptive geometry or range changes; do not
silently assume decoder normalization. Rotation, cropping, color pixel accuracy, source
byte identity and permission/source replacement are separate, still-open gates.

Narrow JNI uses12 integers (expected then actual, each width/height/primaries/transfer/
range/HDR flag); flags require0/1, shapes/enums are checked natively. Kotlin only transports
values. The candidate intentionally performs metadata JNI validation per rendered release;
its allocation/performance cost is unmeasured (no per-frame grading/shader generation).
DecoderColorGate tracks awaiting/verified/failed/closed on the codec owner thread;
every surface render revalidates the current output, not cached proof. Missing/invalid
format or query failure is terminal. Close is idempotent; no reset except a new decoder.

GuardedDecoderFactory delegates audio unchanged. Video creation rejects tone mapping
and input descriptors not admitted by the prepared plan before platform allocation. Its
Codec wrapper intercepts BOTH releaseOutputBuffer(render=true) and timestamp render
release; render=false drops remain allowed for cleanup. Verify before forwarding, never
alter the presentation timestamp. Query/delegate failures latch the gate, invalid renders
never reach the delegate. Wrapper checks owner thread and releases underlying codec at
most once, closing its gate even on release failure. Partial wrapper construction releases
the newly allocated codec. Actual Media3 thread/lifecycle compatibility is NOT established.

Candidate.configure(context,builder) now pairs the explicit frame processor with a
DefaultAssetLoaderFactory using the guarded decoder factory. Transformer1.8.1 has no
Builder.setDecoderFactory API, so do not invent one. No UI/shipping export path invokes
this configuration, starts a candidate codec or bypasses active-grade guards. Candidate
source still has no live preview connection.

The decoder factory callback has Format, NOT URI/clip identity. It admits input matching
a plan descriptor, not a source-order counter or inferred URI association. Equal metadata
is not equal source bytes. Live integration still needs source ownership/freshness and
actual decoder-format/pixel/thread evidence; this gate is not that proof by itself.

## Evidence
Passed locally: six Make/Release CTest suites; GPU/source/decoded-contract ASan+UBSan;
strict production JNI shared build; actual JVM malformed/changed/unknown/HDR decoded
format tests; production Kotlin2.3.21 -Werror/native gate tests for repeated validation,
late geometry/range changes, absent output, failed-query invalidation and terminal close.
Existing plan/projection/pixel/resource/patch tests pass. Shader math unchanged; prior
software GPU result was NOT rerun here and remains separate prior evidence.

Added GuardedDecoderTest (synthetic Codec proxy) for both render overloads, timestamp
preservation, dropped buffers, missing/query/delegate failures, owner-thread rejection
and idempotent release. Android source/test compilation and execution remain **unrun**.
No actual MediaCodec, Android GL, encoded video, preview/seek/cancel/surface lifecycle,
phone, install-over, performance or source identity acceptance. Android CI39958756231
check120431099638 says job did not start due to billing/spending limits, not compiler
success/failure. No new APK; total verified readiness stays25%/75%.

## Official API references (Apache2.0, inspected not copied)
- https://github.com/androidx/media/blob/1.8.1/libraries/transformer/src/main/java/androidx/media3/transformer/Codec.java
- https://github.com/androidx/media/blob/1.8.1/libraries/transformer/src/main/java/androidx/media3/transformer/DefaultCodec.java
- https://github.com/androidx/media/blob/1.8.1/libraries/transformer/src/main/java/androidx/media3/transformer/DefaultAssetLoaderFactory.java
- https://github.com/androidx/media/blob/1.8.1/libraries/common/src/main/java/androidx/media3/common/util/MediaFormatUtil.java

Pinned DefaultCodec converts actual MediaCodec output using MediaFormatUtil; absent
color fields use NO_VALUE, not a new sRGB/SDR default in that conversion. This supports
the candidate boundary design, not a claim that every device reports faithful tags.
