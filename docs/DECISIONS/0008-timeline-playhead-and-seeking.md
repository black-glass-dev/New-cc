# 0008 — Timeline playhead and source seeking

## Status

Implemented source checkpoint; Android execution pending. This follows user-confirmed
basic-hardening phone tests. It is not the entire advanced roadmap.

## Research and independence

- Reviewed Blackmagic Design's public training overview:
  https://www.blackmagicdesign.com/products/davinciresolve/training . It documents
  editing/trim training; it is a UX reference, not a source-code dependency. No
  proprietary implementation, assets, branding or exact layout is used. We do not
  claim to have watched/downloaded its complete courses or manuals.
- Android platform MediaPlayer current-position, seek modes/state rules and seek-complete
  callback reference: https://developer.android.com/reference/android/media/MediaPlayer .
  `seekTo(long, SEEK_CLOSEST)` is available at our minimum API 26. Seeking is asynchronous;
  starting a trimmed clip must wait for seek completion rather than initially playing
  the old/keyframe position. Canvas/View/MotionEvent/AccessibilityNodeProvider are SDK
  APIs; no new dependency, service, native codec library or license is introduced.

## Clock and time domains

The red marker is a projection of the selected preview's **reported media position**,
not an independent stopwatch. Poll only on the owned media looper every 50 ms (20 Hz),
update views on the main thread, and suppress stale generations/seeking/scrubbing.
This is not a 60 fps compositor, frame-perfect preview or a measured A/V-sync guarantee.

The native timeline remains integer/rational at 30 timeline frames per second. Native
`Timeline.resolve_playhead(track, position)` uses half-open [start,end) bounds. It maps
source = source_in + (timeline_position - timeline_start), returns no clip for a gap/end,
rejects negative/unknown tracks and ambiguous overlap. It never mutates the project.
Timestamp conversion retains the core nearest-tick, half-away-from-zero rule. Kotlin
TimelineGeometry only projects cached snapshots/media milliseconds into pixels/timecode
without blocking native disk I/O on each paint. Source offsets are rounded to the same
30-frame lattice and clamped at the clip range; the source seek bar remains explicitly
labelled separately from project HH:MM:SS:FF.

## Interaction and rendering

One Canvas ruler/lanes/red marker share one HorizontalScrollView; short clips no longer
receive fake 84dp timing widths and independent tracks cannot drift out of alignment.
48dp/sec is used normally; long timelines adapt scale to a bounded 100,000px content
width instead of collapsing all later clips at a fixed time cap. Ruler ticks adapt too.

Tap a ruler/clip to seek through native lookup. Drag the marker head to scrub: pause
transport, show provisional time, commit one codec seek on release. Manual horizontal
browsing disables follow; the follow button can reveal the marker again. Clip movement
still requires long-press and routes through the undoable native move command with
free-slot validation. Native history preserves the absolute playhead anchor when
possible instead of jumping to the first clip after Undo. Empty positions pause and
hide stale preview imagery; no gap is silently compacted or rendered in preview.
Playback still hands off only directly adjacent clips and stops at gaps; render gap
compaction still requires the existing user confirmation. No shared compositor claim.

Retain timeline position, follow setting and scroll offset across configuration changes.
Existing export-time cancellation/project retention and explicit SAVE/LOAD autosave
recovery policies remain. Add virtual accessible clip actions, frame-step keys and
48dp transport controls; real TalkBack/large-font/device ergonomics still need checking.

## Verification gate

Host tests: native boundary/source-offset/gap/negative/overlap cases, seeking is read-only.
Android tests: JNI mapping, bounded long-timeline geometry/timecode, real red Canvas
pixels, ruler taps/head scrubs/accessible clip action, real non-keyframe MediaPlayer
position, advancing playback cursor, split/Undo anchor and gap without project mutation.
Run all prior render/publication/history/lifecycle regressions before publishing an APK.

First executed verification: run `39727045607` passed the full suite for `aa57e0d`.
Final review guards and the non-selected-clip drag/preview regression passed in
run `39728248796` for `8f5df51`; portable run `39728248775` passed too. The verified
APK is prerelease `phone-test-39728248796` (6,834,920 bytes). Scope does not change: no compositor, gap rendering,
advanced tools, grading or RAW is claimed.
