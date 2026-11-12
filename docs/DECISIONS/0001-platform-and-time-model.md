# ADR 0001: Portable core and rational timeline time

- Status: Accepted for Phase 0 foundation; revisitable as the Android integration is designed.
- Date: 2026-10-18

## Context

The project requests a C++-first portable engine, Android adapters, offline non-destructive edits, and accurate timestamp handling. The initial repository had no app/toolchain structure. This environment has GCC 12.2 but no JDK, Android SDK/NDK, CMake, or device. Therefore an Android backend choice cannot be honestly built or profiled here.

## Decision

Use dependency-free C++20 for the initial domain foundation. Store timestamps as integer ticks with explicit rational seconds-per-tick timebases; normalize positive timebase terms; compare/rescale using signed 128-bit intermediates; round rescaling to nearest tick with half away from zero. Keep Android-specific code out of the portable core. Defer graphics API, decoder, audio API, and project-storage dependency selection until official documentation and device feasibility tests are available.

## Consequences

Host tests can validate arithmetic and model invariants independently of Android. The signed 128-bit implementation targets GCC/Clang toolchains (including the intended Android NDK Clang) and caps each timebase term at 1e9. This is not a substitute for VFR/frame-boundary/sync tests. Android UI/JNI, MediaCodec, Oboe/AAudio, GPU backend, and export remain unimplemented. No third-party license obligations are introduced in the engine checkpoint.
