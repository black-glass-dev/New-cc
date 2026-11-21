# Project format and persistence checkpoint

## Current schema v2 — per-clip grading (2026-10-23)

Writer always emits **v2**. Reader supports **v0/v1/v2**; v0/v1 clips receive default
neutral grades without timing/identity/media changes. v2 keeps the v1 layout and appends
**149 bytes** of OWNCUTCG v1 immediately after each clip's source_out timestamp:
8-byte magic, uint32 LE version 1, enabled byte 0/1, 17 binary64 LE parameters in
COLOR_PIPELINE order. No length field, sidecar or extra grade IDs. All clips carry a
payload, including neutral audio clips; nondefault audio grades are rejected. Inner
format changes require an outer schema bump. Limits remain 4096 tracks/8192 clips;
new per-clip overhead is 149 bytes (~1.17 MiB at the clip limit).

Old v1-only apps cannot read v2; no downgrade/grade stripping is performed. Current
save/autosave/checkpoint/clear refuse recognized future primary versions, and future
recovery/receipt protections remain. Checkpoint receipt and Android catalog stay v1.
Grade values persist, not the session undo stack. See ADR 0017. Earlier sections below
record prior checkpoints, not an assertion the writer still emits v1.

## Diagnostic checkpoint sidecar (2026-10-23)

At that earlier checkpoint project/autosave payloads stayed schema v1; current writes use v2. `<primary>.checkpoint` is a separate,
bounded (4160 bytes) `OWNCUTCK` v1 diagnostic receipt with generation, session revision,
prepared/committed/clean phase and project ID. It is not a project schema bump, undo
journal, integrity checksum or automatic recovery authority. The primary and recovery
remain independently parsed project documents. See ADR 0014 for ordering and refusal
rules. Unsupported receipt versions and recognized future `OWNCUTPJ` recovery formats
are preserved, not overwritten/cleared. Only explicit malformed-receipt repair archives
the receipt after confirming a readable primary; it never promotes/deletes recovery.

## Status

The portable C++ project codec is wired to app-private filesystem paths for background save/load and autosave recovery. The Android Activity now also has a SAF project-document Save As/Open adapter: it writes/reads the codec's file representation through an app-cache temporary file, then streams to/from a user-selected `content://` document. The Kotlin adapter was built in release `phone-test-39247694047`; its provider behavior still needs physical Documents-provider validation. A selected-clip SAF relink action with undo/redo and source-duration validation is included in APK `phone-test-39250035336`. The newer working-tree command updates all clips sharing the old media reference atomically and undoably; that group change still needs Android build and physical-device checks. Provider-side atomic replacement, persisted project-document grants, crash-recovery journaling, and storage-space checks are not implemented. Linux/Android local path saves now use file+directory `fsync`; this does not guarantee power-loss recovery. Original media is referenced by opaque `media_id` values and is never rewritten by project serialization.

## Current binary schema

All integers are fixed-width little-endian. Signed timestamps use their two's-complement 64-bit bit pattern. Text fields are length-prefixed with an unsigned 32-bit byte length; strings must be valid UTF-8 without NUL bytes and are capped at 4096 bytes. Maximum accepted project data is bounded by 4096 tracks, 8192 clips total, and these per-field caps (worst-case under roughly 86 MB (~82 MiB) in v2). Parsing also validates enum values, booleans, rational timebases, IDs, source ranges, timestamp limits, schema version, truncation, and trailing bytes.

Header: eight bytes `OWNCUTPJ`, then a 32-bit schema version.

- **v0 legacy read-only**: project ID, project name, frame timebase numerator/denominator; track count; for each track: ID, kind, clip count; each clip has ID, media reference, timeline start, source in, and source out timestamps. v0 has no mute/lock fields; migration defaults both to `false`.
- **v1 legacy read-only**: same layout, with one-byte muted and locked flags after track kind. Clips migrate to neutral grades.
- **v2 current**: v1 layout plus the fixed 149-byte grade payload after each clip source_out. Writer emits v2; unknown future versions are refused without changing the source.

Each timestamp stores signed 64-bit ticks and its own positive rational timebase. Timeline start is normalized to the project's frame timebase through the existing timestamp conversion rules; source in/out timebases remain explicit.

## Save behavior and limits

`ProjectStore::save_atomic` writes a temporary sibling file, flushes/closes it, then renames it over the destination. On Linux/Android it additionally `fsync`s the temporary file before rename and the containing directory after rename; other platforms currently use stream flush/close and rename without an explicit platform durability barrier. A failed write before rename leaves an existing destination untouched and removes the temporary file. This relies on same-filesystem rename semantics. `save_autosave` writes a separate `<project-path>.autosave` snapshot; `load_autosave` returns it without changing the main project, and `clear_autosave` removes it only when explicitly called. These steps improve local crash durability but do not provide a journaling/recovery log, schedule autosaves, guarantee behavior on every filesystem/controller, or make the SAF provider write atomic. If directory `fsync` fails after rename, the new file may already be visible even though save reports an error. The current SAF adapter stages bytes through an app-cache file and bounds project stream size to 96 MiB, but writes directly to the selected provider document; provider-side atomic replacement is not guaranteed. Do not pass a `content://` URI as a filesystem path. A provider error during streaming may leave a truncated destination document, although the in-memory project remains loaded. The picker allows arbitrary document types because providers do not consistently expose a MIME type for `.owncut`; the native parser validates format/version and rejects invalid documents.

Host tests cover v1 round trip, v0 migration, locked/muted tracks, truncated/unsupported/trailing data, failed-save preservation, and autosave round trips. The durable-save path has not been power-loss fault-injected; add journal/recovery and actual provider/revoked-permission tests before claiming reliable Android recovery.
