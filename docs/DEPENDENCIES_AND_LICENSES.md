# Dependencies and licensing

## Original wheels and HDR-placeholder mitigation (2026-10-25)

Color editor now uses four native-mapped wheels plus Primary/Channels drag values; no visual
sliders/SeekBars. C++ owns RGB-plane mapping/bounds/master preservation, Kotlin pointer/artwork.
Absent/exact25-byte zero HDR padding is unset; nonzero/malformed data remains blocked, with
PQ/HLG/unknown source checks intact. Possible false-presence fix, not confirmed phone cause.
User reports4K10-bit; still preview remains8-bit bounded proxy, not10-bit pipeline/export proof.
No decompiled/proprietary material, new dependency, export guard or readiness change (ADR0032).

Actual local native color tests and331resource/3theme/17label checks pass. Added JNI/UI test
source pending execution/build. New APK and user-phone acceptance pending;25%/75%.


## Planned fixed-viewer precision redesign (2026-10-25)

Implemented original full-window Color workspace: fixed viewer/footer, separately scrollable
controls, landscape columns, compact status/details, ruler scrubbers/neutral markers/48dp
fine-step controls. Design plan in plans/COLOR_WORKSPACE_REDESIGN.md; no copied assets/UI.
Source/reference display is separate from grading admission: show bounded ungraded reference
when available, explicit empty/error otherwise; only admitted source+bitmap enters grading.
Timeline capture never enters native input. No silent HDR/unknown grading or live/export
fallback. Source failure persists across control changes (ADR0031).

325strings/3themes/17labels and whitespace pass. New hierarchy/reference/fine-step UI regression
source authored; actual Android build and phone behavior pending. No dependency, native
math/export guard or readiness change;25%/75%. Reference is a still, NOT live playback.


## Slider color-editor rework (2026-10-24)

Original3-group/17slider controls replace typing-only fields; debounced automatic still
preview bounds worker work and rejects stale revisions. Apply now saves native-validated
current settings independently of still decoder availability; exact session/clip/history
checks retained. Success does NOT imply graded timeline/export. This supersedes earlier
successful-still-required UI policy. Source/render color safety remains unchanged.

Fix input-only/null raw-format premature live failure; per-render raw gate remains strict.
Show bounded nested/selectable diagnostics, not generic error alone. Exact user-phone cause
and UI behavior remain unverified.308resources/3themes/17labels pass; new pure/UI regression
sources authored, not executed. Build/candidate pending. No proprietary UI/code copied,
no new dependency;25%/75% (ADR0030).


## Color panel discoverability fix (2026-10-24)

User screenshot exposed Color panel detached during final layout assembly. Reattach to
context scroll pane; show labelled theme-aware actions before long help text. Icon-only
navigation unchanged. Regression instrumentation asserts attached/shown actions across
3themes, hidden on Edit; execution remains user-owned/pending. No new dependency, grade
math, export guard or readiness change (25%/75%). New APK build pending; see ADR0029.


## Fresh-install publication result (2026-10-24)

ADR0028 candidate fresh-install-40096664665 atf9f7464 is published: Android/native/GLES
checks and actual APK identity/alignment pass. No new dependency or runtime change.
User-owned phone/codec acceptance remains pending; stable update continuity not asserted.
25%/75%; shipping graded-export guards unchanged. See PROGRESS.md for exact artifact.


## Fresh-install exception (2026-10-24)

No new dependency/tool version/key asset. Fresh-install metadata verifier uses Python
stdlib and existing SDK apksigner/aapt2; existing runner debug signing, never exported
private key. Eight new mock tests do not sign/install real APKs (ADR0028).


## Current build checkpoint (2026-10-24)

No new dependency/tool version/assets. Fix uses existing Media3 Format.NO_VALUE and
GLSL ES100 highp sampler qualifier; removes optional documentation annotation. Pinned
Android dependencies now actually resolve/build. No signing/release redistribution. ADR0027.


## Build recovery tooling (2026-10-24)

No new runtime/library dependency or pinned version. Preflight uses Python stdlib, existing
JDK keytool and SDK apksigner/aapt2. Existing setup actions reused on optional owner-hosted
runner. Nine mock tests need no key/SDK. Real tool/signing execution remains pending;
no key, proprietary code or binary redistributed (ADR0026).


## Draft preview dependency impact (2026-10-24)

No new library/tool/source/asset or license change. Reuses platform dialog ownership,
existing ExoPlayer1.8.1/shared shader adapter and original native validation. No Android
dependency resolution, packaging or signing execution claimed (ADR0025).


## Preview hardening dependency impact (2026-10-24)

No new dependency, copied asset or tool version. Existing Android platform API27 scaled
retriever and API26 bounded-original admission; pure Kotlin UI policies and existing host
compiler/JNI tools reused. Runtime/ABI resolution remains unrun with Android build blocked;
no license/redistribution changes (ADR0024).


## Phase 1B dependency impact (2026-10-23)

No new dependency/tool version. Native grade history and v2 payloads reuse the original
C++ module and standard library; Kotlin uses the existing immutable settings/JNI layer.
No proprietary assets, presets or source imported. Existing optional host JVM/Kotlin
compiler tooling/notices are reused, not redistributed. Full Android dependency/ABI
resolution for this batch has not been run; pinned versions remain unchanged.

## Color phase 1A dependency review (2026-10-23)

No new runtime/library dependency. The original C++20 grading module uses standard
library math, spans, atomics and streams; no proprietary assets/LUTs/algorithms copied.
Kotlin uses existing platform ByteBuffer only. Existing optional host JPype/JVM/Kotlin
compiler tools and retained notices are reused; npm compiler 2.3.21 registry SHA512 was
verified. No new tool binaries/presets are vendored or included as generated artifacts.
This batch does not change the app's existing Media3/toolchain pins or certify all
upstream dependencies as currently audited. Review licenses/maintenance/capabilities
again before choosing future GPU/color-management/RAW libraries.

## Additional optional host compiler check (2026-10-23)

Official JetBrains npm `kotlin-compiler@2.3.21` (repository metadata points to
https://github.com/JetBrains/kotlin, Apache-2.0) was downloaded into development cache;
registry SHA512 integrity was verified and distribution notices retained. It matches
the existing app Kotlin pin and adds no APK dependency. The host script uses its CLI,
the existing cache-only JVM and original Kotlin fixtures. No compiler/JAR is vendored.
This check does not constitute a fresh complete maintenance/advisory audit; re-review
upstream before making this optional compiler download a required build dependency.

## Optional host checks added 2026-10-23 (not shipped)

No new app/runtime dependency. The cache-only host JNI smoke test used **JPype1 1.7.1**
(upstream https://github.com/jpype-project/jpype, wheel metadata `Apache-2.0`), a local JVM
and original fixture bytecode. **jdk4py 25.0.2.1** came from PyPI, upstream
https://github.com/activeviam/jdk4py; its metadata classifies GPLv2 and bundled Java
25.0.2 includes GPLv2 with the Classpath exception and per-module legal notices.
This is a development runtime, not a JDK compiler or an Android build-toolchain change.
Official OpenJDK 17 JNI headers were obtained from `openjdk/jdk` at `jdk-17+35`
(GPLv2 + Classpath exception notices retained in cache). **PyYAML 6.0.3** (MIT) was used
only to parse workflow syntax, not to execute application inputs. These tools and
transitive Python packages are not vendored or included in APKs.

Local distribution metadata/notices and actual execution were inspected; this is not
a fresh full upstream maintenance/security/advisory audit or blanket approval for
redistribution. Re-audit current official releases/notices before promoting any into
required CI or shipped dependencies. Android CI remains on the existing JDK 17 pins.

## Current dependencies

- Engine uses the C++ standard library only; no third-party runtime/library dependency is currently added.
- Host core: CMake 3.20+ is optional; GCC/Clang C++20 compiler is required. GitHub Actions uses `ubuntu-24.04`, `actions/checkout@v4`, and distro CMake/Ninja packages.
- Android build-time pins: Gradle 8.13, Android Gradle Plugin 8.13.2, Kotlin Gradle Plugin 2.3.21, JDK 17, SDK API 36, NDK 28.2.13676358, CMake 3.22.1. Compatibility and wrapper checksums are recorded in ADR 0002 and Gradle files. Gradle wrapper scripts/JAR are upstream Gradle v8.13.0 under Gradle's Apache-2.0 distribution; wrapper JAR SHA-256 is checked against Gradle's published reference.
- Android runtime: no external runtime library dependency; the app uses platform Android APIs and its own native library.
- Android instrumentation tests only: AndroidX Test core/runner 1.7.0 and JUnit extension 1.3.0. These are test-scope dependencies, not packaged in the app APK. The official [AndroidX Test release page](https://developer.android.com/jetpack/androidx/releases/test) lists these as stable (page updated 2026-01-14); upstream [`android/android-test`](https://github.com/android/android-test) is maintained by the Android team and its repository license is Apache-2.0. The current APK build will verify dependency resolution and instrumentation-source compilation.
- CI actions are pinned by major release tags, not immutable commit SHAs; review/pin action SHAs as part of release hardening.
- No app distribution, codec, or third-party binary is present. Repository code is not yet accompanied by a project-level license; determine the intended source/distribution license before publishing or accepting contributions.

## Before adding dependencies

Record upstream source, exact version/commit, maintenance status, license/notices, Android API/ABI and 16 KB page-size compatibility, binary size, security/advisories, and reproducibility. Prefer reproducible source builds and avoid unverified binaries. Recheck official Android/NDK, MediaCodec, Vulkan/OpenGL ES and Oboe/AAudio documentation at the decision point; no runtime backend has been selected.

FFmpeg is not included. If considered, document exact configure flags and LGPL/GPL consequences; do not enable GPL/nonfree components by default. Verify distribution/linking/source obligations and codec patent risks with qualified counsel. This document is engineering guidance, not legal advice; professional legal review is needed before release.

Never commit credentials, signing keystores, private media, or user project files.

## Media3 rendering adapter (2026-10-21)

- AndroidX Media3 Transformer and Effect pinned to **1.8.1**, official AndroidX-maintained
  media project, Apache-2.0. Official release history checked at
  https://developer.android.com/jetpack/androidx/releases/media3 and upstream 1.8.1
  source/API/license checked at https://github.com/androidx/media/tree/1.8.1 .
- Chosen as the bounded Android codec/GPU adapter, not an editing-domain replacement.
  This known patch-line API is pinned for reproducibility; newer release lines exist,
  so upgrades require the render fixture/device matrix, not an untested floating version.
- Uses platform MediaCodec and GLES; no FFmpeg/native decoder extension is added to
  the APK. Our existing JNI ABI/page alignment checks still apply. APK size increase
  and Samsung performance must be measured from CI/device builds.
- FFmpeg via imageio-ffmpeg was used only off-device to create original synthetic
  instrumentation fixtures; neither the tool nor its binaries are app dependencies.

CI emulator reproducibility follow-up: rolling SDK emulator 37.2.12 exited before
guest boot in run 39599894456. Pin the official stable 36.4.9 Linux archive, build
14788078, SHA-256 `85eab2a52517e8cac33dcd7fe3bed43a19472c9c2f17badf1f2616327d71488f`,
verified from https://developer.android.com/studio/emulator_archive . Software GLES
is used without Vulkan for headless codec tests; this is CI tooling, not an APK
dependency. Official archive license notice applies; no emulator binary is committed.

Measured basic-render APK release size: 6,786,632 bytes (`phone-test-39603413359`),
versus 3,350,454 bytes in `phone-test-39431256128` before Media3. These are GitHub
release-asset sizes, not an installed-memory or on-device performance measurement.
The final render CI run executed real codec tests successfully on the pinned emulator;
Samsung/physical-device performance remains unmeasured.


## Project/node architecture batch

No dependency added. Uses existing Android platform Activity/View/AtomicFile/SAF APIs
(min API 26 preserved), existing native project codec, and original C++20 graph contract.
Official public workflow references are linked in ADVANCED_ARCHITECTURE; they are research
references, not redistributed source, assets or licensed product components. Media3 and
AndroidX pinned versions/licenses are unchanged; no GPU/audio/RAW backend selected.


## Studio UI and language batch
No added runtime/build dependency. Original stroke-vector icon drawing uses Android
Canvas/Path; no third-party icon assets/fonts or product UI copied. Configuration,
SharedPreferences, platform dark Material theme and native tooltips already support
min API 26. Python resource gate uses standard library only. Existing Media3/AndroidX
maintenance/license pins are unchanged. ADR 0010 links official Android guidance.

Quiet-editor/theme pass adds no dependency. Palette JSON, platform layouts, existing
Canvas icons and dark-theme variants are original app code/data. ADR 0011 records
official public color guidance; no third-party UI/source/assets redistributed.

Managed-storage/patch batch adds no dependency: platform FileLock/MessageDigest and
existing native codec, SDK apksigner/aapt2/zipalign, JDK signing config, Python standard
library verifier/ledger tests. Secrets are runner-temporary only; no signing key ships
in source or app. UPDATE_APKS documents owner setup and old-debug-key constraints.

Actions currently warns that setup-gradle@v4/upload-artifact@v5 target deprecated
Node20 and are forced onto Node24 by hosted runners. Existing tasks worked through
emulator gates; updating action majors is a separate compatibility checkpoint.
Artifact report upload still hits quota; this does not imply app test success/failure.
Latest CI startup is blocked by account payment/spending limits, not a native build
failure. No signing keys or downloaded reference APKs are repository files.

Native coordinator checkpoint: no new app/runtime third-party dependency. C++20
mutex/thread facilities use the existing standard library/platform Threads. Host JNI
syntax checks used official OpenJDK `jdk-17+35` headers from openjdk/jdk (GPLv2 with
Classpath exception); these temporary check headers are not copied into the project
or app. Local CMake tooling was installed in excluded sandbox cache, not vendored.
Existing app/tool dependency pins are unchanged.

## Phase 1C GPU foundation (2026-10-23)

No new app/runtime dependency or changed Media3 **1.8.1** pin. Android GLES/platform APIs
are existing min-26 facilities. Official Media3 Apache-2.0 public API/working-space/shader
contracts inspected; our GLSL/CPU oracle/harness are original, not copied engine source.
This is not a claim that pinned 1.8.1 is today's latest release; upgrading it requires
rechecking transfer/default contracts and Android integration. See ADR 0018 references.

Local tools only, excluded cache, not bundled/vendor code:
- CMake/Python packages as before; jdk4py **25.0.2.1**, JPype1 **1.7.1**;
  JetBrains kotlin-compiler **2.3.21** npm distribution (SHA512 integrity verified).
- Official OpenJDK17u **jdk-17.0.18+8** jni.h/jni_md.h compile headers (GPLv2 + Classpath
  exception), existing ABI; Android build JDK17/NDK pins unchanged.
- `@sparticuz/chromium` **131.0.1** npm archive SHA512 verified, only temporary software
  renderer extracted: ANGLE **2.1.24077 / 367e9e74a865**, SwiftShader driver **5.0.0**.
  Package wrapper MIT; ANGLE BSD-3-Clause and SwiftShader Apache-2.0 official licenses
  inspected, retained in cache. Package license is NOT assumed to cover all binary parts.
  This older isolated test snapshot is not a maintained product dependency/security or
  Android ABI/page-size endorsement. No Chromium executable runs or ships in the app.
  Latest inspected 153.0.0 GLES libraries expose no public EGL entrypoints and were not
  usable by this harness; don't upgrade/assume driver compatibility without checking.
- CI adds distro-managed Mesa EGL/GLES software-test libraries on Ubuntu24.04 (Mesa MIT
  components + distribution notices; not app dependencies). New CI step has not run here.

Host GPU script uses ctypes/standard Python/C++/developer-provided EGL/GLES; downloads
nothing, leaves probes temporary. No binary/media assets, private source or shader files
from another editor are copied, and no tool artifacts/driver libraries enter Git/APK.

## Phase 1C source/plan continuation (2026-10-23)
No new app/build dependency or pin change. Uses existing Android MediaExtractor/MediaFormat
(minAPI26), pinned Media3 1.8.1 APIs, native standard library and immutable Kotlin transport.
Official C.java1.8.1 inspected through GitHub to verify SRGB constant; no third-party source
copied. Same excluded-cache OpenJDK17u headers, jdk4py25.0.2.1, JPype1.7.1 and SHA512-verified
kotlin-compiler2.3.21/CMake tools as ADR0018 checks; no tooling enters Git/APK. New source
metadata schema is ephemeral, not a persisted file-format/runtime dependency change.

## Candidate decoder gate (2026-10-23)
No added dependency/pin change. Uses existing Media3 Codec/DefaultAssetLoaderFactory and
platform Surface; LogSessionId is only the existing nullable Media3 factory parameter,
never constructed on old Android. Official Apache2.0 Codec/DefaultCodec/MediaFormatUtil/
asset-loader API source inspected, not copied (ADR0020 links). Same excluded host tools/
licenses as prior checkpoint. Android min26/ABI/build/security support remains unverified
for this adapter; no new runtime binary, SDK mirror or private-source dependency added.

## Color controls/still-preview batch (2026-10-23)
No new runtime/build dependency or license pin. Original three-circle color icon and
platform Android dialog/widgets/MediaMetadataRetriever/Bitmap/Executor (min26) plus existing
native/Media3 adapters. No external images/fonts/LUTs or proprietary UI/source copied.
Host tools unchanged. Actual Android/lint/codec/memory/ABI/install-over checks are pending;
no new APK/security/performance or full grading claim (ADR0021).

## Experimental live clip preview (2026-10-24)
Explicit app dependency media3-exoplayer1.8.1 aligns with existing Media3 effect/transformer;
Apache2.0, same pin (not a latest-maintenance claim). ExoPlayer was already used transitively
by Transformer; actual APK-size/ABI/min26/runtime effect is not measured. Official pinned
public/protected renderer/graph/ExoPlayer APIs inspected, not copied; no proprietary source,
assets or internal replay reflection. Existing host tool licenses/pins unchanged and caches
excluded. Android build/lint/performance/device acceptance pending; no binary vendored/APK.

## Native diagnostics / encoded test source (2026-10-24)
No new dependency/pin or persisted format. Original standard C++ counters and JNI/Kotlin
transport; Android test fixture authors frames with platform EGL/GLES/MediaCodec/MediaMuxer
(min26). No downloaded media, LUT, copyrighted assets or vendor YUV-layout assumptions.
Existing cache-only host compiler/JVM tools/licensing unchanged. Android fixture/encoder/
codec/profile/performance acceptance remains unverified; no binary asset or APK added.


### Inline viewer candidate (2026-10-25)

See ADR0033: source-probed guarded video fallback, relative no-jump wheels, lifecycle
release/recreate-paused and compact feedback. No dependencies or safety-gate relaxation.
User confirms previous Play draft works, not this new inline path. Local geometry/JNI
checks pass; Android/phone acceptance pending. Readiness25%/75%; no10-bit export proof.

Final ADR0033 source168a106 built/published as fresh-install-40202669644; Android40202669644
and Portable40202669634 PASS. UI regression sources compile only, not executed. Phone
inline-viewer/paused-adjustment acceptance pending; do not equate build success with visible
grading or10-bit output. Existing dependencies unchanged. Latest evidence: BUILD_AND_TEST.


### Preview-input / polish candidate — 2026-11-03

See ADR0034. Source unknown is not Rec709 proof; second parser can recover tags, explicit
preview-only missing-tag interpretation is opt-in/revocable and must stay visibly labelled.
Never allow it into export/project persistence or override known HDR/non-709 tags. Native
hue pixel tests and compact original UI/feedback added. No dependencies or readiness change.
Android/device validation pending; user reports latest prior draft fails, not current success.

ADR0034 final candidate734b947 published as fresh-install-41635353326. Android41635353326
and Portable41635353307 PASS; actual JNI/Kotlin eligibility and native hue tests PASS. UI
regressions compile only; device execution/phone acceptance pending. Explicit Input override
must stay preview-only/labelled/revocable; strict normal export unchanged. No new dependency,
10-bit proof, source-accuracy proof or readiness credit. Latest evidence: BUILD_AND_TEST.


### Focused live-work candidate — 2026-11-03

ADR0035: do not trailing-debounce every wheel move or discard every in-flight CPU frame.
GPU work stays one-active/latest-pending with draw tokens; paused redraw via public cache,
bounded by watchdogs (phone exact-frame verification pending). One focused wheel and compact shared controls; no new dependency.
Actual Kotlin/JNI tests pass; Android/phone proof pending. Keep HDR/source and explicit
Input safeguards; no10-bit/export or readiness claim (25%/75%).

ADR0035 final sourceedaabbc published as fresh-install-41641807647. Android41641807647 and
Portable41641807536 PASS. UI regressions compile only; phone cached-redraw/continuous-drag,
polish and GPU-resource acceptance pending. Final candidate uses public replay cache and
stable effect, NOT the earlier muted pump prototype. Source/HDR/Input/export guards retained.
Existing Media3 pin/license unchanged; cache uses two SDK-owned textures. Readiness25%/75%;
no10-bit, output parity or device-performance proof. Latest evidence: BUILD_AND_TEST.


### Visible geometry candidate — 2026-11-03

ADR0036: compare pre-rotation visible geometry derived from validated inclusive crop, not
raw allocation size. C++ owns bounds/math; narrow JNI transports seven ints. Never round
codec dimensions, swap width/height, infer HDR/depth or drop source/decoder gates to get
playback. Source/decoded raw Details and stage retained. User reports previous candidate
all preview paths fail geometry; new Android/phone validation pending.25%/75% unchanged;
existing Media3 API reviewed, no dependency/license or project-schema changes.

## ADR0037 preview output-format sequencing (2026-11-03)

User again reports no graded live frame/Play; screenshot is REFERENCE ONLY. Installed
build identity is unconfirmed because supplied exception lacks ADR0036 diagnostics.
Preview validation now follows Media3's buffer-associated output-format event, rather
than polling potentially newer asynchronous codec state for each older buffer. Native
validation still runs before every render; codec release clears the bound descriptor and
invalid/missing/changed/malformed formats remain terminal. Details adds version/code/source
revision/SDK and explicit bilingual Copy report. No telemetry, guessed geometry, source/HDR
or export relaxation. Real Kotlin/JNI sequence regressions and344 bilingual/3theme/17label
checks PASS; Android/phone pending. No measured phone root cause or10-bit output proof.
Dependencies unchanged; domain C++ and narrow JNI retained. Readiness25%/75%.

## Original implementation / large-batch execution (ADR0038)

User confirms live video preview works well on their phone; export still fails. Work now
follows [the retained Bengali batch plan](plans/OWN_IMPLEMENTATION_BATCH_PLAN_BN.md):
export reliability → shared preview/export grade → primaries → curves/LUT → scopes →
color management/advanced tools → lifecycle/editing → paid-release hardening. No Graden
code/assets or GPL integration. Android/Media3 licenses still apply; no dependency added.
C++ domain and narrow JNI retained; no grading sliders. Agent owns routine implementation
decisions; user input only for consequential scope/license/data risk or essential device evidence.

Audit confirms makePlan/composition reject active grades in shipping export; candidate is
not wired to normal export. Guards remain: no silent ungraded substitute. First batch adds
preflight and bounded selectable/copyable failure reports for preparation/render/gallery/
document-copy, version/revision and clip/grade/preset context. Explicit clipboard only;
no telemetry. Nested error messages may contain platform paths. Existing cancellation/
cleanup and gap-consent remain. Local native6 / actual pure Kotlin report tests / Python27 /
345 bilingual3themes17labels PASS; Android and phone export pending.25%/75% unchanged.

## ADR0039 — Parallel ownership / raw export capability

Three-agent plan: docs/plans/THREE_AGENT_WORK_PLAN_BN.md (A Android/export/integration,
B new native curves, C independent QA). Workers not started here; independent workspaces/
assigned branches, restricted file ownership, PR/patch review before integration. No new
dependency/license change. Optional RawVideoOutputFormatProvider now feeds current-buffer
raw crop/color into native validation in GuardedCodec. Missing/malformed/provider failure
never falls back to lossy Format; non-provider backend keeps strict behavior. Synthetic
regressions added; actual raw-preserving MediaCodec backend still unimplemented, shipping
graded export blocked. User-confirmed live preview retained.25%/75%, no new phone proof.

## ADR0040 — Original surface decoder candidate backend

Own synchronous SurfaceVideoDecoder implements public Media3 Codec plus raw-current-buffer
evidence, with pure DecoderBufferLease epoch/EOS/terminal state. Candidate configure now
selects this backend; default audio/plain shipping export unchanged. API29+ lossless surface
backpressure, no raw evidence polling while held, native inclusive crop normalization and
strict native color/geometry gate. Input ownership, bounded event polling, checked timestamps,
EOS completion and idempotent release implemented. No GPL/dependency/shipping guard change.
Local actual pure Kotlin10000-epoch lease tests PASS; Android real decoder test source added
(not executed here). Device/GPU/export parity and unsupported device quirks remain unverified;
full roadmap not complete. Readiness25%/75%. See ADR0040 for scope/limitations.
