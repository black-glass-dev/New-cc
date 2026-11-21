# ADR 0003: Command history and portable project persistence

- Status: Accepted as a foundational checkpoint, not a release storage contract.
- Date: 2026-10-18

## Decision

Model user edits as C++ commands; implement move, split, left/right trim, delete, and single-track ripple delete. Ripple delete shifts clips beginning at or after the deleted clip end only on the selected track; overlapping clips and other tracks remain stationary. Keep undo and redo stacks in memory with a 256-command default cap (configurable), clear redo history after a successful new edit, and do not persist the history yet. Serialize project identity, frame timebase, track state, stable IDs, media references, and source/timeline timestamps in a bounded little-endian binary format. Writer emits schema v1; reader migrates the documented v0 layout by defaulting absent track mute/lock fields to false, and rejects unsupported future schemas. Save to a same-directory temporary path and rename after flush/close.

## Rationale and limits

The core currently has no vetted JSON/SQLite dependency, Android URI adapter, or project data ecosystem, so a small versioned codec avoids adding a dependency before its maintenance/license/security costs are justified. The codec caps text fields at 4096 bytes, tracks at 4096, and clips at 8192 total. The format is private/internal, not an interchange standard. Local rename protects the existing file from ordinary serialization/write failures. Linux/Android saves now fsync the temporary file and parent directory, but this is not a write-ahead journal or tested power-loss guarantee; Android SAF still needs provider-specific atomic replacement and persisted URI grants. Before release, review whether to retain/evolve this format or migrate to a documented interoperable format, and add explicit migration fixtures for every shipped schema.

Basic reliability update: native media insertion now executes `AddClipCommand`, not
an untracked Timeline mutation. Undo removes the import; redo restores its stable
clip ID/media/range/placement before later split/move commands are replayed. Failed
commands preserve prior redo history. This fixes the last-import UI undo regression.
Project format is unchanged; command history remains transient on project reopen.
