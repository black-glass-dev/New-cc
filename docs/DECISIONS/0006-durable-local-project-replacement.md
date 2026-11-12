# ADR 0006: Sync local project files around atomic replacement

- Status: Accepted for Linux/Android local project paths; not a full crash-recovery journal.
- Date: 2026-10-19

## Context

The project store already writes a temporary sibling and renames it over the previous file, but an `ofstream::flush` only moves data to the operating system; it does not ensure the file and directory entry are durable across a sudden power loss on Linux/Android.

## Decision

After closing the temporary stream, Linux/Android builds reopen the temporary file and call `fsync`; after same-directory rename they `fsync` the parent directory. Other platforms keep the existing stream-flush/rename behavior until a platform-specific durability adapter is added. Autosave uses the same path.

## Consequences and limits

- Existing target remains intact for failures before rename; the new directory entry is explicitly synced on Linux/Android.
- Directory-sync failure occurs after rename may already have committed; the method reports failure although the new file can be visible.
- This is not a write-ahead journal, cannot guarantee hardware/controller behavior, and does not cover SAF provider writes.
- Host tests verify round trips and failed-write preservation, not simulated power loss or filesystem fault injection.
