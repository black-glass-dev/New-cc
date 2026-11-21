# ADR 0015 — Pinned native lifetime and revision-cached timeline projections

2026-10-23. Native/JVM and pure Kotlin host tests passed; Android acceptance pending.

## Native lifetime

JNI handles were raw pointers. Kotlin synchronized lifetime in ordinary use, but an
invalid/stale handle or an in-flight call racing nativeDestroy could dereference freed
memory. SessionRegistry now allocates positive monotonic int64 tokens, never addresses
and never reused during this library lifetime. Lookup pins a shared owner for the call;
close removes the token so future lookups fail, while existing calls can finish safely.
Duplicate/unknown closes are idempotent. Invalid lookups become Java argument errors.
Final destruction occurs outside the registry lock. Token exhaustion refuses allocation.
Registry lifetime is process-local, not persisted identity or a security capability.

This closes the native lifetime race, **not** the Activity/controller task lifecycle.
It does not retain UI callbacks, make workers recreation-aware, or solve a new Activity
trying to open a managed project before the old Activity releases its writer lease.
SessionCoordinator still serializes edit/I/O; Kotlin still synchronizes wrapper access.

## Timeline transport and caching

Add nativeGetClipPage(offset, limit, expectedRevision) and nativeGetRevision. Page limit
is 1..256 entries; revision is an exact uint64 decimal string, not a JSON double or
signed-long sentinel. Negative/out-of-range offsets, malformed revisions and torn reads
are rejected. Pages preserve existing clip JSON fields and track order. A page copies
only its clip range: skips whole tracks and indexes directly instead of scanning all
previous clips per page. Complexity is O(track count + page entries), not O(offset).
The legacy whole-snapshot endpoint remains for compatibility/tests, not Android reads.

EngineSession uses a pure Kotlin TimelineSnapshotCache. An unchanged revision returns
one immutable list without rebuilding JSON/DTOs. A changed revision fetches bounded
pages and publishes only after cursor, revision, size, progress and total consistency
checks. Interrupted/malformed reads leave the previous cache intact. Close clears it.
The complete list is still retained for editing/export, and a changed read temporarily
holds old/new lists: **not** full UI-memory virtualization. Entry bounds are not a fixed
byte limit on media identifiers/JSON; no phone-memory/performance figure is claimed.

## Draw projection

TimelineClipIndex sorts start intervals and stores prefix maximum ends. Conservative
viewport queries preserve original painter order, support overlaps, include pixel-edge
clips, and explicitly include a moved clip even if its original position is offscreen.
Lane and display-name projections rebuild only for changed clip values. This avoids a
full rectangle/drawing pass for ordinary sparse visible ranges, but pathological long
overlaps can still scan many intervals. Hit testing/accessibility and full timeline
storage remain unchanged; this is not complete accessibility/UI virtualization.

## Evidence and next gates

Make three suites; CMake/CTest 3/3; session/registry ASan+UBSan; production JNI under
real host JVM -Xcheck:jni (including parallel reads/destroy, stale/forged handles,
revision pages, invalid args and stale-page rejection); production pure Kotlin compiled
with app-pinned compiler 2.3.21 -Werror and run on host JVM. Kotlin tests check 8192
intervals against brute force, boundaries/overlaps/order, 32-page cache reads, immutable
publication, cache hits, invalid/interrupted pages, changing totals and clear/rebuild.
Android regression sources add native-page/cache/viewport-projection cases but are not
compiled/executed here. No Android/ART/codec/update result or APK is claimed.

Next: controller/task ownership across busy recreation, source/bin identity/offline
work, truly bounded UI consumers and actual persisted/evaluated graphs. Current Flow
remains read-only. Roadmap credit stays 25/75; evidence updated, not inflated.
