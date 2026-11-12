# ADR 0005: Stage SAF project documents through bounded temporary files

- Status: Accepted for the early Android editor shell; provider durability remains incomplete.
- Date: 2026-10-19

## Context

The portable C++ project codec currently reads and writes filesystem paths, while Android's Storage Access Framework exposes user-selected projects as `content://` URIs. Passing the URI string to the native path API is invalid, and adding provider APIs to the portable codec would couple domain storage to Android.

## Decision

Keep the C++ codec path-based. The Kotlin platform adapter uses `ACTION_CREATE_DOCUMENT` / `ACTION_OPEN_DOCUMENT`, stages through a unique app-cache `.owncut` file, and streams document bytes through `ContentResolver`. The stream copy is bounded to 96 MiB. Loading parses the staged document before replacing the current native project. Save As clears the app-private autosave snapshot only after the document stream succeeds. No third-party dependency is added.

## Consequences and limitations

- Project data remains portable and original media is not copied or rewritten.
- The user chooses a project document each time; project document URI grants are not persisted.
- The provider is written directly, so provider-side atomic replacement and power-loss durability are not guaranteed; a failed provider write may leave a truncated destination.
- SAF picker behavior, provider errors, and real-device Save As/Open remain to be tested. The instrumentation source tests bounded stream-copy behavior, not a real DocumentsProvider.
- A later reliable save design may require provider-specific temporary-document/rename support or a format journal; do not claim crash-safe project persistence from this adapter.
