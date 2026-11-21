# ADR 0002: Android shell, toolchain, and JNI smoke boundary

- Status: Accepted for the initial app-shell checkpoint; versions may be updated through an explicit compatibility review.
- Date: 2026-10-18

## Context and verified sources

The previous checkpoint had no JDK, Android SDK/NDK, Gradle, or device. Official documentation was checked before pinning the initial setup:

- [AGP 8.13 release notes](https://developer.android.com/build/releases/agp-8-13-0-release-notes): AGP 8.13 supports API 36.1 and requires Gradle 8.13 and JDK 17; the page documents AGP 8.13.2.
- [Kotlin Gradle plugin compatibility](https://kotlinlang.org/docs/gradle-configure-project.html): Kotlin Gradle plugin 2.3.21 supports Gradle through 9.3 and AGP through 9.0, so it is compatible with this AGP/Gradle pair.
- [Android NDK CMake guide](https://developer.android.com/ndk/guides/cmake): AGP `externalNativeBuild` uses the NDK CMake toolchain.
- [Android 16 KB page-size guide](https://developer.android.com/guide/practices/page-sizes): NDK r28+ emits 16 KB ELF-aligned shared libraries by default. CI also validates APK native-library alignment with `zipalign -P 16`.
- [SAF document provider guide](https://developer.android.com/guide/topics/providers/document-provider) and [`takePersistableUriPermission`](https://developer.android.com/reference/android/content/ContentResolver#takePersistableUriPermission(android.net.Uri,%20int)): `ACTION_OPEN_DOCUMENT` is used for document-backed media and a persisted read grant is attempted.
- [MediaMetadataRetriever API](https://developer.android.com/reference/android/media/MediaMetadataRetriever) and [MediaPlayer API](https://developer.android.com/reference/android/media/MediaPlayer): metadata extraction is performed off the UI thread; MediaPlayer is created and all player calls/callbacks are confined to a dedicated `HandlerThread` because the API is not thread-safe and requires a Looper for callbacks.
- [Gradle release checksums](https://gradle.org/release-checksums/): Gradle 8.13 binary distribution and wrapper JAR checksums are pinned in this repository; the checked-in wrapper JAR matches the published SHA-256.

## Decision

Pin Gradle 8.13 (wrapper distribution SHA-256), AGP 8.13.2, Kotlin Gradle plugin 2.3.21, JDK 17, compile/target API 36, min API 26, NDK 28.2.13676358, and CMake 3.22.1. Use a single Android application module and a small platform `Activity` (Views for the bootstrap only); do not add Compose or external runtime dependencies until the editing workspace work begins. Build the existing portable engine into `libowncut_android.so` through a coarse-grained JNI API. Kotlin owns the native session lifetime and synchronizes calls; editing commands, status snapshots, and project file operations cross JNI only on user actions, never per-frame/per-pixel. Instrumentation test creates a native session, adds/splits/trims/deletes a clip, exercises undo/redo, saves/loads a project and autosave, and closes the session.

Only `arm64-v8a` and `x86_64` are initially packaged. They are build/test ABI coverage, not a promise of universal device compatibility.

## Consequences and limitations

The shell validates startup, native load/link, a single batched query, and explicit close. It is not an editor UI; no media, audio, renderer, project persistence, or export exists. Android build and emulator execution must be green in CI before this setup is called verified. The local environment still cannot run Android tooling. CI is configured to check build, Android lint, instrumentation on API 35 x86_64, and 16 KB APK alignment. Add a real 16 KB device/emulator run and physical ARM64 device matrix before release.
