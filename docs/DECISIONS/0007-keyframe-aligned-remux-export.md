# ADR 0007: Offer a guarded, lossless MP4 timeline remux

- Status: Accepted as an initial export subset; not full render/export acceptance.
- Date: 2026-10-19

## Context

The app needs an output path, but it does not yet have a shared decoder/compositor/encoder graph, resampling/audio mixer, or device-verified transcode pipeline. Publishing an export feature that silently ignores trims, gaps, or sync would be unsafe.

## Decision

Use Android's platform `MediaExtractor` and `MediaMuxer` for a lossless stream-copy MP4 export of a deliberately narrow plan: one video track, clips in non-overlapping timeline order, and each clip starting on a video sync sample. Multiple original URIs may be joined only when their video and optional audio track formats are compatible, including codec MIME, codec-specific data, dimensions, profile/level, color metadata, and orientation; incompatible sources fail before muxing. Source duration may differ by at most half a 30 fps frame plus 1 ms endpoint-rounding allowance from its rounded timeline-frame duration. After explicit user confirmation, empty timeline gaps (leading or internal) are omitted and occupied clips are concatenated in order; the project itself is not changed. At most one audio track is copied. Samples beyond each clip's rounded output duration are skipped to prevent timestamp overlap. Timestamp monotonicity is checked, and extractor sample flags are translated to muxer flags; encrypted and partial-frame samples are rejected. After muxing, the output is reopened to validate tracks/sample timestamps and its first frame is decoded before it is copied to SAF. Unsupported edits fail with an actionable message; there is no re-encoding, compositing, effects, cross-track mixing, subtitle/caption preservation, or arbitrary-frame-accurate cut.

Output is staged in app cache before copying to a SAF document. Cancellation is checked while muxing and copying. A canceled/failed copy to the selected provider may leave an empty or partial destination document; provider deletion/atomic replacement is not assumed.

## Consequences and validation limits

- Compatible split/trim timelines can produce an MP4 without an external codec dependency or quality loss.
- Most arbitrary split points are not keyframes and will be rejected; no smart-render/transcode fallback exists.
- Container/codec combinations still depend on the device's Android extractor/muxer support.
- Planner instrumentation tests validate structural constraints only. Actual sample export, playback, A/V sync, cancellation, and format compatibility require physical-device testing.
- This is not the final shared render graph or general export acceptance gate.

## Superseded default path: basic render export (2026-10-21)

The remux-only default does not satisfy basic arbitrary split/trim acceptance.
MainActivity now calls TimelineRenderExporter with a snapshot of the same validated
single-track plan. AndroidX Media3 owns platform decoder/GL/encoder scheduling on an
owned HandlerThread. Both audioNeedsEncoding/videoNeedsEncoding return true, trim
optimizations/transmux are not enabled, and output requests H.264/AAC, source video cadence, 720-high
SDR with aspect ratio preserved. Silent clips receive silence. HDR is explicitly
tone-mapped to SDR, not silently treated as preserved HDR. Device fallback may reduce
encoder parameters; unsupported codecs fail without publishing a Gallery row.

Gap compaction still requires explicit confirmation; overlap/multitrack rendering,
effects UI, grading and RAW remain out of this basic checkpoint. Native C++ retains
edit/time/project ownership; Kotlin is the codec lifecycle adapter. Output is validated
before Gallery publication. A codec hang is bounded to 30 minutes. Release APK
publication now follows successful instrumentation, not merely compilation.

Verification update: run `39603413359` for `4837c7f` passed emulator instrumentation,
including untouched input re-encoding, arbitrary non-keyframe trim and heterogeneous
three-clip rendering. `phone-test-39603413359` was published only after those tests.
The remux-only exporter remains as historical/internal code, not the UI export path.
Physical-device A/V sync and lifecycle acceptance remain separate from emulator tests.
