# ADR 0013 — Portable session ownership before Android controller extraction

2026-10-22, phase B implementation, Android verification pending.

The JNI-owned Project/CommandHistory/mutex and import-ID allocation move into the
original C++20 SessionCoordinator. All edit, undo/redo, read, save/load and recovery
entry points lock the same owner; JNI only converts arguments/results and constructs
existing typed commands. Kotlin's synchronized handle methods still protect lifetime
against close. The native object must not be destroyed while calls remain outstanding.
This is not an asynchronous task executor or a replacement for managed FileLock leases.

Revision increases on successful mutation/state replacement, including undo/redo;
failed edits/loads and missing recovery do not advance it. Save marks the native saved
revision only after atomic write/fsync succeeds. Dirty is conservative: undo back to
identical saved content still requests a checkpoint. Revisions/history are **not**
persisted and are not yet the Android UI's dirty-state authority. Managed checkpoint
validation and catalog updates can fail after native save, so Activity keeps its own
unsaved/error state until that larger transaction succeeds.

Commands take a timeline backup before history execution, undo or redo. On exception,
the timeline is restored. History execution already preserves prior stacks when a
new command throws. Custom commands must still obey strong exception guarantees for
their own internal undo/redo bookkeeping. O(N) timeline backup is a safety/performance
trade-off, not a claim of large-project/mobile throughput improvement.

Full owned project snapshots support portable consumers/tests. JNI timeline JSON
instead reads pages of at most 256 clip values at an expected revision; a changed
revision rejects a torn read instead of returning mixed state. Existing JSON fields,
track order and 30 fps Android presentation conversions are unchanged. The complete
JSON/Kotlin list still accumulates all clips: **not** end-to-end bounded UI memory or
virtualization. Snapshot paging has an entry bound, not a new URI/string byte budget.
Media append uses the native timeline's rational rate (normally 1/30), not UI widgets.

Explicit save_copy does not mark a primary checkpoint clean; MainActivity's SAF
staging export uses it. Explicit restore_draft reads a validated snapshot without
marking it saved; ManagedProjectStore recovery uses it after stable bounded copying.
Corrupt replacement parses before assignment. Recovery never promotes/clears primary.
Existing .owncut v1 writing, v0 read migration and media-reference semantics remain.

Host evidence: Make core + session tests; strict CMake Release/CTest; ASan+UBSan;
JNI C++ syntax against official OpenJDK 17 JNI headers. Header-only host validation
is not Android ABI/link/device testing. NativeCoordinatorContractTest adds two Android
bridge cases for >256 clips, escaping, independent sessions, copy/draft load, corrupt
replacement and post-load ID collision avoidance. They are added, not yet executed.

Remaining phase B: Activity/controller lifecycle extraction, durable revision journal,
full transaction/fault-injection recovery, bounded UI consumption and Android regressions.
Prior install-over failure and signing/billing blocks are unchanged. Readiness remains
25/100 credited; new coordinator package is in progress, not counted complete.
