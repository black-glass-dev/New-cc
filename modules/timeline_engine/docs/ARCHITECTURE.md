# OwnCut Mobile Video Timeline Editing Engine (`timeline_engine`)

## 1. Module Isolation & Coordinator Boundary
- **Module Root:** `modules/timeline_engine/`
- **Exclusive Build Directory:** `build_timeline/`
- **Standalone & Subdirectory Build:** Builds independently via `cmake -S modules/timeline_engine -B build_timeline` and integrates cleanly via `add_subdirectory(modules/timeline_engine)` exposing target `timeline_engine::timeline_engine`.
- **Zero Color-Engine Coupling:** `timeline_engine` contains **no dependency** on `modules/color_engine/`. All color grading passes execute via the zero-dependency hook in `include/timeline/color_hook.hpp`.

---

## 2. Clean Color-Hook Contract (`include/timeline/color_hook.hpp`)
`Timeline::render_frame_with_color_hook()` delivers decoded linear/float RGBA buffers (`FrameBufferView32F`) across two explicit passes:
1. **`process_clip_frame(FrameBufferView32F& frame)`**: invoked per active clip with `clip_id`, `track_id`, `group_id`, and `RationalTime64`.
2. **`process_timeline_frame(FrameBufferView32F& frame)`**: invoked after multi-track compositing (with active adjustment clip ID when present).

Both C++ (`IColorPipelineHook`) and C ABI (`ColorPipelineHookC` / `owncut_tl_register_color_hook`) contracts are supported so an external coordinator can plug `color_engine` into `timeline_engine` in <10 lines of wiring code without modifying either module's internals.

---

## 3. Architecture Overview (Phases 1–4)

### Phase 1 — Core Infrastructure, Memory & Media Pool
- `RationalTime` (`rational_time.hpp/.cpp`): 128-bit intermediate normalized rational timestamps across `23.976`, `24`, `25`, `29.97`, `30`, `50`, `59.94`, `60`, and `120` fps with zero arithmetic drift and bidirectional SMPTE Drop-Frame / Non-Drop-Frame timecode parsing/formatting.
- `FrameMemoryPool`, `LockFreeRingBuffer<T>` (SPSC), `LockFreeMPSCQueue<T>` (`memory_pool.hpp/.cpp`): 64-byte cache-line aligned arena slabs (`std::align_val_t(64)`), wait-free audio/control ring buffers, and lock-free task queues.
- `TaskScheduler` (`task_scheduler.hpp/.cpp`): 4 priority lanes (`RealtimePlayback`, `TouchScrubbing`, `SmartRenderCache`, `BackgroundProxyWaveform`) with a **dedicated isolated high-priority worker thread** ensuring heavy background proxy/waveform work never starves real-time playback or touch scrubbing, plus coalescing keys and cancellation tokens.
- `MediaPool` & `ProxyCacheManager` (`media_pool.hpp/.cpp`): `IMediaDecoder` abstraction (`MediaCodec` / `VideoToolbox` / optional `FFmpeg` / deterministic `SyntheticMediaDecoder`), Thumbnail & Waveform Mipmap Pyramids, Smart Bins, Flags, Color Tags, Star Ratings, and LRU Smart Render Cache.

### Phase 2 — Cut + Edit Timeline Engine
- `Timeline` (`timeline_model.hpp/.cpp`): `Project -> Timeline -> Track (Video/Audio/Subtitle/Adjustment) -> ClipItem` (`SourceClip`, `CompoundClip`, `AdjustmentClip`, `MulticamClip`, `GeneratorClip`, `TitleClip`), Markers, Presentation Annotations, Tabbed/Stacked Timeline Manager, `compare_timelines` Diff, Dual Overview/Detail Timeline Controller, `SourceTapeController`, `SmartIndicator`, and 18 RGBA32F blend modes.
- `CommandStack` (`command_stack.hpp/.cpp`): reversible snapshot/composite command history with drag coalescing.
- `EditOperations` (`edit_operations.hpp/.cpp`): all 7 Cut/Edit modes (`SmartInsert`, `AppendAtEnd`, `PlaceOnTop`, `Overwrite`, `RippleOverwrite`, `Replace`, `FitToFill`), `SwapClips`, `ShuffleClips`, separate A/V In/Out (J/L-cuts), `BladeSplit`, `RippleDelete`, `Lift`/`Extract`, cross-timeline Clipboard Cut/Copy/Paste, Magnetic Snapping with haptic callbacks, Compound Clip creation, and Multicam Angle switching.
- `TrimEngine` (`trim_engine.hpp/.cpp`): `TrimIn`, `TrimOut`, `RollEdit`, `SlipEdit`, `SlideEdit`, `AsymmetricMultiTrackTrim`, `SafeTrimmingMode` handle clamping, `DynamicJKLLoopTrim` shuttle/loop controller, and `ABGraphicalTrimmer` handle inspection.
- `RetimeEngine` (`retime_engine.hpp/.cpp`): piecewise Bezier `SpeedPoints`, Freeze Frame, Reverse, Newton-Raphson `CubicBezierCurve`, `Nearest`, `FrameBlend`, real **Pyramidal Lucas-Kanade `OpticalFlow`** (with AArch64 NEON intrinsics under `__aarch64__` and scalar fallback), `ISpeedWarpProvider` hook, and `SmoothCut`.

### Phase 3 — Animation, Text/Subtitles, Vision & Fairlight Audio Core
- `AnimationEngine` (`animation_engine.hpp/.cpp`): keyframed transform rig (`Linear`, `Hold`, `EaseIn`, `EaseOut`, `EaseInOut`, `CustomBezier`), `Loop`/`PingPong`/`OffsetRepeat`, `DynamicZoomSpec`, Keyframe Tray navigation, JSON `InspectorPreset` serialization, and 7 video transitions.
- `MultiTextEngine` & `SubtitleEngine` (`text_subtitle_engine.hpp/.cpp`): UTF-8 multi-layer text rendering (Bangla/English), exact `.srt` / `.vtt` / `.ttml` parsers & exporters, and `IMotionGraphicsRenderer` (Lottie / OGraf).
- `VisionEngine` (`vision_engine.hpp/.cpp`): optical-flow `Stabilizer` (Kalman & Gaussian trajectory smoothing + `CameraLock` + adaptive zoom), `SmartReframe` (saliency-tracked 16:9->9:16/1:1 reframing), `SceneAndCutDetector` (histogram + MAD shot boundary & boring/jump-cut removal), `TextBasedEditor`, and `IntelliScriptMatcher`.
- `AudioEngine` (`audio_engine.hpp/.cpp`): `SyncBinController` (Timecode + Cooley-Tukey Radix-2 FFT Cross-Correlation + 4/9/16/25 grid + Live Overwrite), 6-band RBJ Biquad `ParametricEQ6Band`, `DynamicsProcessor` (Compressor, Limiter, Gate, Sidechain Ducker), BS.1770 `LoudnessMeter` (LUFS + 4x True-Peak dBTP), lock-free `AudioMixer`, `LiveVoiceoverRecorder` (SPSC ring-buffer + countdown + teleprompter), spectral-flux `BeatDetector`, and `IAudioAIEnhancer`.

### Phase 4 — Mobile UX & Platform Bridges
- `MobileGestureController` (`mobile_ux.hpp/.cpp`): Portrait/Landscape layout solver, Pinch-to-Zoom, 2-Finger Undo, 3-Finger Redo, Virtual Jog Dial with haptic detents, Long-Press Loupe Trim.
- Thin Bridges: `bridge/timeline_c_api.h` + `src/timeline_c_api.cpp`, `bridge/android_jni.cpp` (`__ANDROID__`), `bridge/ios_bridge.mm` (`__APPLE__`).

---

## 4. Build & Test Instructions
```bash
cmake -S modules/timeline_engine -B build_timeline -DCMAKE_BUILD_TYPE=Release
cmake --build build_timeline -j
ctest --test-dir build_timeline --output-on-failure
```
For sanitizer verification:
```bash
cmake -S modules/timeline_engine -B build_timeline_san -DOWNCUT_TIMELINE_SANITIZERS=ON
cmake --build build_timeline_san -j
ctest --test-dir build_timeline_san --output-on-failure
```
(Clean up any temporary scratch build directories before handoff, keeping `build_timeline/` intact.)

---

## 5. Owncut Android App Integration (`OwncutTimelineAdapter`)
- **C++ Adapter:** `include/timeline/owncut_adapter.hpp` + `src/owncut_adapter.cpp` bridges `Owncut`'s 30fps string-ID `SessionCoordinator` / `ClipSnapshot` model into `timeline_engine` (`Timeline`, `MediaPool`, `MagneticSnapEngine`, `MobileGestureController`, `DualTimelineController`).
- **Root CMake & Android Shared Library:** Root `CMakeLists.txt` embeds `modules/timeline_engine` via `add_subdirectory` (`OWNCUT_TIMELINE_BUNDLE_JNI=OFF`) and compiles `bridge/android_jni.cpp` directly into `libowncut_android.so` so all `com.owncut.timeline.TimelineBridge` JNI symbols are exported cleanly without static-archive stripping.
- **Kotlin UI & Session Wiring:**
  - `app/src/main/java/com/owncut/timeline/TimelineBridge.kt`: `TimelineEngineBridge` + `@JvmStatic external` JNI bindings (`nativeSyncClips`, `nativeEvaluateSnap`, `nativePinchZoom`, `nativeResetZoom`, `nativeGetZoomScale`, `nativeGetCapabilitiesJson`, plus edit/split/undo/redo C API hooks).
  - `app/src/main/java/com/owncut/app/EngineSession.kt`: keeps revision-cached timeline snapshots automatically synced to `TimelineEngineBridge` and releases native handles in `close()`.
  - `app/src/main/java/com/owncut/app/TimelineView.kt` & `MainActivity.kt`: live 2-finger `ScaleGestureDetector` pinch zoom + native magnetic snap evaluation with visual snap guide line and `HapticFeedbackConstants.CLOCK_TICK`.

