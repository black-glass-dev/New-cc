# ADR0022 — isolated experimental live clip grade preview

2026-10-24. Implemented source; Android build/runtime/device acceptance PENDING.

## Why isolated
Phase1C needs real video processing, not only CPU stills. Preserve user-approved timeline
MediaPlayer generation/seek/lifecycle/audio behavior until the new player has evidence.
Add an explicit experimental action in Color, not a silent replacement or a production
export guard bypass. It plays ONE immutable saved clip snapshot, not an editable timeline
composition or uncommitted Color draft. No project/history/autosave mutation.

## Pipeline
Worker raw-source admission -> shared ColorRenderPlan -> native immutable GLES recipe ->
ExoPlayer1.8.1 with a specialized default MediaCodecVideoRenderer -> renderer-owned
PlaybackVideoGraphWrapper -> SingleInputVideoGraph.Factory(shared explicit DEFAULT SDR
frame processor). The protected graph-creation hook avoids ExoPlayer's implicit working
space factory. Audio uses DefaultRenderersFactory/ExoPlayer, with audio focus, one clipped
MediaItem, repeat-one, clip-relative seek and explicit pause/play. No timestamp effects.
Before switches the native grade effect off, not the color conversion; comparison is
only enabled during playback because this adapter has no paused-frame replay cache.

At output-format change validate both input Format and actual raw output against admitted
geometry/color. Before each non-decode-only output buffer reaches the sink, validate the
current raw codec output again with the terminal native metadata gate. Unknown/HDR/range/
geometry drift refuses, never falls back to ungraded playback. Renderer exceptions surface
as player errors, pause and disable controls. This metadata check still does not establish
URI byte identity or calibrated pixel accuracy, and strict coded geometry/range can reject
valid decoder normalization. Per-buffer metadata JNI/query overhead remains unmeasured.

The renderer specializes the standard implementation, inheriting effect message handling
and graph creation/release ownership. Public ExoPlayer docs warn effects don't support
general custom/extension renderers; the protected default-renderer hook is used here, but
this is still EXPERIMENTAL and needs actual Android compatibility proof. No external sink
ownership/reflection into internal replay APIs. Graph/codec/TextureView shutdown stays
with ExoPlayer; renderer closes its gate after release. Dialog close/background destroys
this player; legacy timeline remains paused. Closed-state checks prevent delayed probe
completion from starting another player. Release exceptions log, never crash destruction.

Progress transport uses exact overflow-safe floor(duration*progress/1000); tested across
all1001 positions at Long.MAX_VALUE against BigInteger. No timeline time-domain edits.
ExoPlayer explicit dependency remains aligned1.8.1/Apache2.0 with existing Media3 modules;
not the latest-version claim or a measured APK-size/performance/ABI endorsement.

## Actual evidence and boundaries
Passed: Make6; Release CTest6/6; strict production JNI; real JVM/native; production pure
Kotlin2.3.21 -Werror plan/gate/ARGB/draft/projection + new progress arithmetic checks;
298 bilingual strings/3themes/17labels per language,7metadata and3install-over checks.
Original source/API inspected, not copied; shader math unchanged, no new GPU run.

Added LiveColorPreviewTest raw output-format mapping/missing/HDR/terminal-gate assertions.
Android adapter/test compilation, live decode/GL/audio, actual frame comparison, surface/
background/close races, loop/seek/thread behavior, crop/rotation, memory/performance and
encoded-export parity are NOT run. No working APK or full phase1C/Color completion claim.
Normal timeline MediaPlayer and graded export guards stay. Verified readiness25%/75%.

## Official pinned API references (Apache2.0)
- https://github.com/androidx/media/blob/1.8.1/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/video/MediaCodecVideoRenderer.java
- https://github.com/androidx/media/blob/1.8.1/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/video/PlaybackVideoGraphWrapper.java
- https://github.com/androidx/media/blob/1.8.1/libraries/effect/src/main/java/androidx/media3/effect/SingleInputVideoGraph.java
- https://github.com/androidx/media/blob/1.8.1/libraries/exoplayer/src/main/java/androidx/media3/exoplayer/ExoPlayer.java
