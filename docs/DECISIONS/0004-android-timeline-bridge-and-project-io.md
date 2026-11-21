# ADR 0004: Batched Android timeline bridge and app-private project I/O

- Status: Accepted for the initial Android editing shell; subject to device validation.
- Date: 2026-10-18.

## Context

The Activity needs to display native-owned timeline clips and invoke existing C++ commands without creating a Kotlin shadow model. ProjectStore currently accepts filesystem paths only. Metadata extraction, MediaPlayer interaction, and project persistence must not block the UI thread.

## Decision

- Return one escaped JSON timeline snapshot per JNI call, with clip/track/media IDs, timeline frame start/duration, and source in/out milliseconds. Kotlin parses this immutable result for the clip list; C++ remains the owner of edit state.
- Keep edit/history JNI calls coarse-grained and synchronized by the native session. Kotlin owns only selection and Android UI state.
- Invoke native project save/load/autosave on a single-thread scheduled executor. Debounce edit autosaves, save a final best-effort recovery snapshot during Activity shutdown, and require explicit confirmation before loading a saved project or replacing the current session with a recovery snapshot.
- Store the initial active project under app-private files storage because the core ProjectStore is path-based. Do not represent this as SAF project-file support.

## Consequences

- The JSON snapshot adds allocation/parsing overhead and uses the bootstrap's fixed 30 fps timebase for display. A typed JNI batch or event-based snapshot may be justified after profiling.
- Project media IDs remain URI strings; reopening requires the Android document grant to remain valid. There is no relink UI or portable project bundle yet.
- Saving on destruction is best effort, not a crash-safe journal. Linux/Android local replacements now fsync the temporary file and containing directory, but power-loss behavior is not fault-tested and SAF provider writes are not atomic. Autosave recovery is explicit and never silently overwrites the saved project.
- The Activity, JNI ABI, Android storage behavior, and media lifecycle still require Gradle/NDK and physical-device validation.
