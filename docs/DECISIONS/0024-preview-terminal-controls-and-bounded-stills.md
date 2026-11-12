# ADR0024: Terminal preview controls and bounded CPU still requests

Date: 2026-10-24. Status: implemented source; Android validation pending.

## Decision

Experimental live preview owns a main-thread PREFLIGHT/BUFFERING/READY/FAILED/CLOSED
policy. Failure/close are terminal: stop callbacks and preflight work, null the player
reference before release, disable transport and reject late READY/first-frame events.
Release failures are logged without restoring ownership. Buffering allows pause but
not seek/Before; comparison requires active playback, not unsupported paused replay.
Seeking pauses transport, suspends ticker thumb updates, then restores playback intent.
Keyboard/accessibility progress changes seek without assuming a touch drag. Clip end is
exclusive, so 100% targets duration−1ms, not EOS. Clamp source positions before subtracting
the clip-in point to avoid overflow on malformed/extreme transport values.

For still previews, request an oriented, aspect-fit, no-upscale proxy within1280×720 via
API27 getScaledFrameAtTime; reject unexpected bitmap size/config/color space rather than
silently converting it. API26 permits only metadata dimensions already fitting that
budget and otherwise presents a bilingual Android8.1+ explanation. Unknown geometry
fails, not full-resolution fallback. Check cancellation around expensive work.

These are platform/UI transport policies, not duplicate C++ grading or color admission.
The request bounds the returned CPU proxy, NOT codec-internal allocations, export size,
or decoded GPU surfaces. Scaling before still grading is still not established parity
with export grading before output sizing. No source-time rounding or timeline edit change.

## Evidence and limits

Pure production Kotlin policy tests cover terminal-event sequences, controls, exclusive
seek bounds, extreme positions and100size/rotation combinations; compiled with pinned
Kotlin2.3.21 -Werror. Native6/6, actual JVM/JNI, existing Kotlin adapters,299bilingual
strings/3themes/17labels,7metadata/3install-over tests pass. No new dependencies.

Android app/test sources are UNCOMPILED/UNRUN: actual callback/release ordering, buffering,
audio, accessibility behavior, rotated extraction and decoder memory remain device gates.
Latest Android CI run40083307621 never started (billing/spending-limit annotation), not a
compiler verdict. No new APK, phone evidence, codec/GPU/parity claim or shipping guard
removal. Readiness remains25% complete/75% remaining.
