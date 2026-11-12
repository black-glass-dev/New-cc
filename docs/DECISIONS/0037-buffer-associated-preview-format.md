# ADR0037 — Bind preview validation to Media3 output-format sequence

2026-11-03. Accepted; phone runtime acceptance pending.

The user again reports geometry rejection, no Play and no inline/experimental live frame.
Their screenshot explicitly shows REFERENCE ONLY, not graded. The supplied exception lacks
ADR0036's expected/actual and stage diagnostics. Installed build identity therefore remains
unconfirmed; do not blame installation or assume the previous fix worked on this source.

Research: official MediaCodecRenderer API and pinned Media3 1.8.1 source:
https://developer.android.com/reference/kotlin/androidx/media3/exoplayer/mediacodec/MediaCodecRenderer
https://github.com/androidx/media/blob/1.8.1/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/mediacodec/MediaCodecRenderer.java

`drainOutputBuffer` handles INFO_OUTPUT_FORMAT_CHANGED and associates output Format by
presentation timestamp using updateOutputFormatForTime before processOutputBuffer. Codec
adaptation-workaround output is filtered upstream. Our repeated codec.outputFormat query
bypassed this association and could read a later asynchronous format for an earlier buffer.
This is an adapter risk, not a proven measurement of the user's phone failure.

Bind an immutable normalized descriptor from onOutputFormatChanged; validate it there and
again before every render via native gate. Clear on codec release. Invalid/missing/change/
malformed events and close remain terminal; no fallback to a previous known-good descriptor.
No dimensions are guessed, swapped, rounded or resized. Crop/color/HDR/export guards remain.
New pure Kotlin sequence is codec-thread transport/lifecycle only; domain checks remain C++.

Details now includes versionName/code, CI source revision, sequence marker and SDK plus
existing stage/codec/expected/raw/source diagnostics. Explicit bilingual Copy report writes
to the local clipboard only; no telemetry/provider URI/CSD dump. No claim of device playback,
GPU correctness, end-to-end10bit or export parity. Readiness25%/75%.

Tests: actual Kotlin/JNI repeated100 frame guards, single event transport read, codec clear,
changed dimensions, malformed event and close terminal behavior PASS. Existing crop tests
and344 bilingual strings/3themes/17labels PASS. Android build and phone pending at commit.
