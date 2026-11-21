# ADR 0017 — Per-clip grade values, atomic history and project schema v2

2026-10-23. Phase 1B native implementation/host tests; Android acceptance pending.

## Ownership and editing

Clip owns a validated color::Grade value, keyed by stable clip ID, not source URI.
New imports and legacy files default to neutral. A split copies the grade to both
halves; trim/move/relink retain it. Delete/ripple undo restores the value. Audio tracks
must have default neutral grades; color edits and moves of graded clips to audio refuse.
Timeline::replace_clip remains timing-only, never an alternate grading API.

SetClipGradesCommand applies a uniform grade to unique targets in one undo entry.
A temporary timeline validates every target (including locks/audio) before publication.
Failure does not partially grade the live timeline, advance revision or clear redo.
History retains only old grades plus owned target IDs, not duplicated source URIs.
Set/reset/bypass/copy-paste use this command; bypass preserves parameters. SessionCoordinator
serializes it through the established command/history owner. Full temporary timeline
copies remain: this is not a realtime or byte-budgeted history optimization. History
itself is session-local; reopening/restoring clears undo, while grade values persist.

## Project migration and safety

Writer now emits OWNCUTPJ **v2**, keeping v1's fields/order and appending exactly 149
bytes of OWNCUTCG v1 to each clip after source_out. The preset codec stays standalone
when used alone, but its fixed payload now also represents a project's clip grade.
Reader supports v0/v1 (neutral grade migration) and v2; malformed/truncated/nonfinite/
unsupported inner grades fail before live state replacement. The inner payload version
is fixed for schema v2: changing that contract requires an outer project schema bump.
Track/catalog/checkpoint receipt versions do not change. Project IDs/timing/URIs do not
change. Recovery/checkpoints/save-copy use the same native project codec, not sidecars.

This is backward-reading, **not** backward-writing compatibility: older apps cannot
open newly saved v2 documents. No silent v1 downgrade/grade stripping. Recognized
future project versions refuse primary save, checkpoint, autosave and recovery clear;
future recovery/receipt guards remain. Preflight happens before checkpoint recovery
writes. Logical interruption tests preserve graded draft/primary at all six boundaries;
these are not physical power-cut guarantees or a catalog/native multi-file transaction.

## Bridge, immutable reads and honest delivery

Native JSON pages now include enabled + 17 full-precision parameters and native
Pipeline::identity-derived gradeActive. Decimal serialization uses max_digits10, not
six-decimal to_string. ClipSnapshot/ColorGrade are value/immutable projections;
owned native snapshots and existing export plans retain the grade captured at creation.
EngineSession exposes set/reset/bypass/copy operations through a narrow JNI group API.
These are internal APIs awaiting live UI/dirty-state integration, not completed controls.

Current MediaPlayer/Transformer do not render the new grade. Selected-clip information
explicitly warns about ungraded preview. Active graded clips refuse both render and
legacy remux export/planning, including directly supplied plans, to prevent silently
losing a look. Neutral/bypassed/zero-strength grades still follow existing export policy.
The gradeActive projection is trusted native metadata; callers must not fabricate or
mutate its relationship with the immutable grade. Remove the guard only after phase
1C shared color-managed processing and encoded-output parity tests pass.

## Evidence and next gate

Five C++ suites; CMake/CTest 5/5; grade/project suite ASan+UBSan; strict JNI shared build;
real JVM -Xcheck:jni grade/history/persistence/precision/invalid-target tests; production
pure Kotlin DTO/cache and ColorEngine bridge tests. Android managed-grade/storage/export
contract sources added but full Android compilation/ART/phone/GPU/codec testing unrun.
Next phase 1C: explicit input color conversion and shared preview/export processing;
UI remains phase 1D. No new grading APK, no complete color feature or readiness credit.
