## Non-starving live preview / focused wheel candidate (2026-11-03)

User says734b947 improved but wheel live preview still fails; no new phone trace. ADR0035
fixes provable trailing-debounce and CPU-frame discard starvation, adds latest-only GPU
work acknowledged by actual shader draw and bounded muted paused-frame decoding. Play uses
inline draft so controls remain accessible. Focused one-wheel UI replaces four competing
cards; shared compact controls and accessible icon actions. Source/HDR/Input safeguards
unchanged. Actual pinned Kotlin/JVM/JNI queue/old-policy tests and343 resources PASS;
Android/UI/device verification pending. No phone-success,10-bit or performance claim.
Readiness25%/75%, existing dependencies unchanged. See ADR0035 and DEVICE_TEST_MATRIX.

## Color-input recovery and compact wheel APK PUBLISHED (2026-11-03)

[Download updated APK](https://github.com/islamyousuf032-glitch/New-cc/releases/download/fresh-install-41635353326/owncut-fresh-install-debug.apk).
Source734b9472aa018bd6846ee4320be94a7d26a60c55; Android41635353326 / Portable41635353307 PASS.
Assembly, lint, UI-test source compilation, APK alignment and publication complete. GitHub
release API:8,067,959bytes, SHA256
`6f0b24abcbea744f93cc56b687bd81569b7ec87df77f4feb9c612e79ddb1884f`.
No independent local APK rehash claimed (release-assets host restricted).
Local Make6, strict JNI build/real JVM+Kotlin2.3.21 -Werror, native24 hue-direction cases,
6palette rankings,108 metadata eligibility cases, color ASan+UBSan,342 bilingual/3themes/
17labels and27 Python policy tests PASS. Android UI/device tests compile, NOT executed;
emulator/install-over skipped. Phone metadata recovery/visible grading/polish acceptance
pending. Earlier168a106 Play draft failure remains user evidence, not disproved by CI.

ADR0034: second-parser source-tag recovery; Input → Interpret missing tags is explicit,
revocable PREVIEW-ONLY Rec709 SDR interpretation, not source proof. Known HDR/non-709/
conflicting tags remain blocked. No normal graded-export,10-bit output or parity claim.
Original compact six-hue wheels/shared controls and numeric feedback; no grading SeekBars.
Fresh debug signing; back up before any required uninstall, no update-continuity guarantee.
Readiness25%/75% unchanged. Full acceptance matrix below / DEVICE_TEST_MATRIX.

## Preview metadata and wheel-polish candidate (2026-11-03)

User now reports168a106 Play draft fails with explicit Rec709-primaries admission error;
previous draft success is historical, not current. ADR0034 adds second-parser tag recovery
and explicit preview-only Input interpretation for missing tags (never known HDR/non-709),
original six-hue compact wheels, directional native pixel regressions and numeric feedback.
Source/HDR payload unmeasured; no phone success,10-bit output or graded export claim.
Local Make6/resource341/3themes/17labels/27Python checks PASS; Android/Kotlin candidate tests
pending.25%/75% unchanged. Existing pinned Media3 API reviewed, no new dependencies/licenses.

## Inline grading viewer APK PUBLISHED (2026-10-25)

[Download inline viewer + relative wheel APK](https://github.com/islamyousuf032-glitch/New-cc/releases/download/fresh-install-40202669644/owncut-fresh-install-debug.apk).
Source168a1069e4f0722aa854f87d6726fe5ebbbdb46b; Android40202669644 and Portable40202669634 PASS.
APK assembly, lint, device-test source compilation, identity/alignment and publication PASS.
Release API reports8,063,583bytes and SHA256
`fc78dbf91618939af1f0bcb65a427c8678f97a7a8d67c59fecab2b4c58bb933b`.
Local Make6, actual JVM/JNI + pinned Kotlin -Werror (including fresh-frame/no-drift bounds),
335 bilingual/3theme/17label checks and27 Python policy tests PASS. No local binary rehash:
release-asset download is restricted in this workspace; size/digest above are GitHub release API evidence.
UI regressions compile but were not executed; phone/emulator/10-bit output acceptance remains pending.
User confirms previous de4bad3 Play draft works; this new inline fallback is not yet phone-proven.
Back up before any required uninstall; temporary debug signer, no update-continuity promise.
ADR0033; normal graded timeline/export still guarded. Readiness25%/75% unchanged.

## Inline viewer / relative wheel candidate (2026-10-25)

User phone evidence: previous Play draft works and UI improved; editor still remains
reference-only with truncated error. ADR0033 adds guarded inline video fallback after
still failure, Before/Play/Refresh integration, paused recreation after background,
relative no-jump wheels, cancel-safe gestures and restrained feedback/compact chrome.
Actual local JVM/JNI + pinned Kotlin -Werror and335 bilingual/3theme/17label checks PASS.
Candidate Android build/UI execution and phone acceptance pending; no end-to-end10-bit,
normal graded timeline/export or premium-feel proof. Readiness unchanged25%/75%.

> **Historical user-approved phone-tested APK:** [Quiet editor + three themes](https://github.com/islamyousuf032-glitch/New-cc/releases/download/phone-test-39777165014/app-debug.apk)

## Native wheel workspace APK PUBLISHED (2026-10-25)

[Download wheel workspace APK](https://github.com/islamyousuf032-glitch/New-cc/releases/download/fresh-install-40199335976/owncut-fresh-install-debug.apk).
Source de4bad3; Android40199335976 and Portable40199335868 PASS. Actual APK assembly/lint/
instrumentation-source compilation/identity/alignment/publication complete.8,045,179bytes;
SHA25697627b9f8eb07049fe8cfd3f80b5e2dc58eb5ff450452620f201e65ae6f3787b.
Local Make6, color ASan+UBSan, strict production JNI compile and real JVM/JNI + pinned
Kotlin2.3.21 -Werror adapters pass wheel/placeholder and prior regressions.332strings/
3themes/17labels and8fresh/9signing/7metadata/3install-over policy checks pass.
UI/device tests compile, not executed. Four original wheels/drag values replace sliders;
8-bit proxy is NOT10-bit end-to-end proof. User's exact4K/10-bit source/HDR payload/live
behavior remains unmeasured; placeholder fix not blanket HDR support. Normal graded
playback/export still guarded. Fresh install may require uninstall after backup; no
stable signer continuity. No proprietary code/assets or readiness credit;25%/75% (ADR0032).


## Original wheels and HDR-placeholder mitigation (2026-10-25)

Color editor now uses four native-mapped wheels plus Primary/Channels drag values; no visual
sliders/SeekBars. C++ owns RGB-plane mapping/bounds/master preservation, Kotlin pointer/artwork.
Absent/exact25-byte zero HDR padding is unset; nonzero/malformed data remains blocked, with
PQ/HLG/unknown source checks intact. Possible false-presence fix, not confirmed phone cause.
User reports4K10-bit; still preview remains8-bit bounded proxy, not10-bit pipeline/export proof.
No decompiled/proprietary material, new dependency, export guard or readiness change (ADR0032).

Actual local native color tests and331resource/3theme/17label checks pass. Added JNI/UI test
source pending execution/build. New APK and user-phone acceptance pending;25%/75%.


## Precision workspace APK PUBLISHED (2026-10-25)

[Download new Color workspace test APK](https://github.com/islamyousuf032-glitch/New-cc/releases/download/fresh-install-40165432408/owncut-fresh-install-debug.apk).
Source57879db; Android40165432408 and Portable40165432394 PASS. Actual assembly/lint/
regression-source compilation/identity/alignment/publication complete. APK8,034,423bytes;
SHA256 fec88f23d08b9465e5fbe8f382be6c496486445d100e232fa5d2e5b705ae0fa7.
Reference/hierarchy/fine-step instrumentation source compiles; actual UI/device execution
is UNRUN/user-owned. Fixed-viewer layout, original ruler controls and explicit reference/
empty states implemented, not phone-accepted. Platform reference does not establish source
color or permit grading unknown/HDR. Native/export gates unchanged. Backup before uninstall;
temporary debug signer/update compatibility not guaranteed.25%/75% (ADR0031).


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


## Slider editor test APK PUBLISHED (2026-10-24)

[Download slider editor test APK](https://github.com/islamyousuf032-glitch/New-cc/releases/download/fresh-install-40101757559/owncut-fresh-install-debug.apk).
Source1f1bd21; Android40101757559 and Portable40101757571 PASS. Actual assembly/lint/
Android regression-source compilation/alignment/identity checks/publication complete.
APK8,008,831bytes; SHA25689ff87e012a7f69fa5f6cfd1fbdec13b1e1bb3054e0fa5332fa3a3fedfef9adf.
Pinned Kotlin2.3.21 -Werror pure tests now pass34,017slider steps, saved-range preservation,
bounded/cyclic diagnostics plus existing preview/projection policies. Reused reviewed
jdk4py25.0.2.1/npm kotlin-compiler2.3.21 tooling; tarball SHA512 verified outside repository.
Earlier new-source-unrun/build-pending notes are historical. UI instrumentation execution,
actual phone/live/source/codec acceptance remain UNRUN/user-owned. Apply saves validated
settings, not a promise of graded timeline/export. No claim all live failures fixed.
Fresh-install signer may require uninstall after backup. No readiness credit;25%/75%.


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


## Color panel fix APK published (2026-10-24)

[Download corrected fresh-install test APK](https://github.com/islamyousuf032-glitch/New-cc/releases/download/fresh-install-40097805457/owncut-fresh-install-debug.apk) (8,004,471bytes, source d454c0b).
Android40097805457 and Portable40097805462 PASS; actual compile/lint/test-source compile/
alignment/APK identity checks and release publication complete. Color final-layout
attachment and labelled actions implemented; regression source compiled, not executed
on phone. Prior pending-build notes below are historical. User owns phone acceptance.
Temporary debug signer/update continuity not guaranteed: back up before any uninstall.
SHA256 db6ca7507a6688bc7633f362b569a93650c0b7203aef728734651ec895164fec.
Graded-export guards remain; readiness25%/75%.


## Color panel discoverability fix (2026-10-24)

User screenshot exposed Color panel detached during final layout assembly. Reattach to
context scroll pane; show labelled theme-aware actions before long help text. Icon-only
navigation unchanged. Regression instrumentation asserts attached/shown actions across
3themes, hidden on Edit; execution remains user-owned/pending. No new dependency, grade
math, export guard or readiness change (25%/75%). New APK build pending; see ADR0029.


## Fresh-install candidate PUBLISHED (2026-10-24)

[Download test APK](https://github.com/islamyousuf032-glitch/New-cc/releases/download/fresh-install-40096664665/owncut-fresh-install-debug.apk) — 8,004,475bytes; [release/checksum/metadata](https://github.com/islamyousuf032-glitch/New-cc/releases/tag/fresh-install-40096664665).
Source f9f7464; Android40096664665 and Portable40096664742 PASS. Actual APK assembly,
lint, Android test-source compilation, alignment, signature/package/version verification
and separate fresh-install publication passed. This supersedes earlier publication-pending
notes. Phone/device/codec acceptance remains UNRUN/user-owned; graded-export guards stay.
Back up projects/media before uninstalling. Temporary debug signer, NOT an update-compatible
patch. APK SHA256:965754bc673c8a1f034432982460d334ac94a16e4fbb66f1e6bc8abd195d45e7.
No readiness credit for phone validation;25%/75%.


## Explicit fresh-install test APK exception (2026-10-24)

User now authorizes uninstall/fresh install for this test, superseding the earlier mandatory
install-over requirement for this candidate only. Separate debug-signed fresh-install
prerelease route requires an explicit commit marker, actual signature/package/version
checks and passing Android build gates. Stable update route remains unchanged. Back up
projects/media before uninstall; app-local data/settings may be lost. Temporary signer
may change; future update compatibility NOT guaranteed. Publication pending (ADR0028).


## Verified Android build and sampler fix (2026-10-24)

Production code atb802517 passes Android40095688463: APK assembly, lint, Android test-source
compilation and ZIP alignment. Portable40095688482 passes native CTest and96×512production
GLSL/CPU cases on Mesa llvmpipe with unchanged2/255 RGB gate and exact alpha/identity/bypass.
Fixes: Format.NO_VALUE API and unavailable optional annotation; explicit highp sampler
instead of default lowp precision. Regression assertions added (ADR0027).

Historical UNCOMPILED/billing-startup statements below are superseded for this code; real
phone/live/encoder/export tests remain UNRUN. Stable signing not configured in these runs:
no downloadable update-compatible APK published. No guard removal/readiness credit;
25% complete/75% remaining. Phone testing remains user-owned.


## Android build checkpoint (2026-10-24)

Run40095260838 at47005f5 passed actual APK assembly, lint, Android test-source compilation
and ZIP alignment. Earlier UNCOMPILED statements below are historical; source is now
compiled, device/codec/live/export tests still UNRUN. Stable signing unavailable in that
run; no update-compatible candidate published. Host GLES failure remains separate/open.
No phone acceptance or readiness credit;25%/75%.


## APK build recovery (2026-10-24)

[বাংলা সমাধান নির্দেশিকা](docs/APK_BUILD_RECOVERY_BN.md): owner billing correction or an
explicit trusted self-hosted runner, plus original signing identity. Manual runner choice
and pre-build key/pin/approved-APK checks are implemented, not executed. No new APK.


## Unsaved draft playback (2026-10-24)

Color editor now offers experimental live playback of the current draft without applying
or saving it. Native shader validation captures immutable settings; Apply still requires
a successful matching still render. Edited/stale stills are explicitly marked. Android
compilation/phone verification and a new update-compatible APK remain pending (ADR0025).


## Preview usability hardening (2026-10-24)

Experimental preview now has terminal failure/close ownership, buffering-aware controls,
non-jumping touch seek and keyboard/accessibility seek. Still preview requests bounded
API27+ CPU proxies; API26 refuses oversized sources instead of decoding a full bitmap.
Host policy/native/JNI checks pass; Android behavior/build/APK remain unverified. Shipping
guards and25%/75% readiness unchanged. See ADR0024.

> — contextual tools, focused portrait workspace, persisted theme and app language; emulator regression verified.
> Save important project documents externally before changing debug installations.

> **Forward design:** [Owncut advanced architecture v1](docs/ADVANCED_ARCHITECTURE.md)
> and [বাংলা workflow guide](docs/WORKFLOW_BN.md) describe Projects → workspace → typed node processing → delivery, with implemented
> versus planned capabilities and phased acceptance gates. Current Flow is read-only.

# Owncut

Owncut is an original, offline-first Android video-editing project. Its product direction takes inspiration from professional post-production workflows without copying another product's source, branding, assets, algorithms, or exact UI.

## Current checkpoint

**Encoded-grade validation groundwork:** native frame error metrics and a real candidate
encode/decode test source now cover neutral, opposite per-clip exposure and bypass around
a cut. Diagnostics passed native/JNI/Kotlin tests; **Android fixture/encoder tests are
uncompiled/unrun**, proposed codec tolerances are not measured results. No guard removal,
new APK or completed color-parity claim. [ADR0023](docs/DECISIONS/0023-native-comparison-and-encoded-grade-test.md).


**Experimental live grade preview (source added, Android unverified):** Color can now
open an isolated ExoPlayer clip preview using the same native grade/explicit SDR factory
as candidate export, with trim-bounded loop, seek/pause and Before during playback. Raw
decoder format gates refuse unsupported output; close/background releases the player.
Normal timeline playback and user delivery stay unchanged/guarded. Native/host tests pass;
Android build, codec/GL/audio and pixel parity are pending, no new APK. [ADR0022](docs/DECISIONS/0022-isolated-live-clip-grade-preview.md).


**Color editor now has a real processing route in source:** a contextual workspace with
17 editable parameters, native-processed scaled still preview, Before/After, reset/bypass
and atomic Apply/history/autosave. English/Bengali labels and numeric entry are supported.
**Still preview is not live graded playback.** Candidate encoding now has a real test-entry
runner, but Android build/UI/codec output are unverified; normal grade export guards stay.
Host native/Kotlin/JNI tests pass, no APK or completion claim. [ADR0021](docs/DECISIONS/0021-clip-color-controls-and-native-still-preview.md).


**Phase 1C decoder boundary:** native expected/output format validation and a terminal
per-render color gate now pass native/JNI/pure-Kotlin tests. Candidate configuration
installs a guarded Media3 decoder factory; both surface-render release overloads check
current output metadata before forwarding. **Android adapter/codec compilation and real
output remain unverified**; live preview/export guards stay, no APK. CI39958756231 did
not start due to billing/spending limits. [ADR0020](docs/DECISIONS/0020-decoder-output-color-gate.md).


**Phase 1C continuation:** worker-only raw video-color probing, native source-admission
policy and an immutable per-clip color plan now have source/host implementation. Candidate
composition binds each grade before global 720p scaling and carries an explicit processor
factory. **Not running in live preview/export**; Android compilation/codec parity remain
pending. Six native suites and actual Kotlin/JNI plan checks pass. Existing grade guards
stay; no new APK or readiness credit. [ADR 0019](docs/DECISIONS/0019-probed-color-plans-and-candidate-composition.md).


**Phase 1C started — shared GPU foundation (not live preview/export):** C++ now
owns explicit sRGB/linear709/Media3-SDR709 texture recipes and a matched CPU oracle.
Six native suites, sanitizer, real JNI/Kotlin and software EGL/GLES shader comparisons
pass: **96 cases ×512 pixels, max RGB difference 1/255**, exact alpha/identity/bypass.
Candidate Media3 adapter bundles an explicit working-space factory and rejects unknown/
HDR metadata; **Android adapter compilation/device integration remain unverified**.
The MediaPlayer preview and Transformer exporter are unchanged; active-grade guards stay.
No new APK, no hardware/codec claim; verified roadmap remains **25% / 75%**.
[ADR 0018](docs/DECISIONS/0018-explicit-sdr-texture-contract-and-native-gpu-recipes.md)
records the pinned Media3 transfer mismatch and remaining connection gates.


**Phase 1B:** C++ clip-owned grades, atomic grouped undo/reset/paste, schema v2 save/
recovery with v0/v1 neutral migration, and exact immutable grade snapshots are now
implemented/host-tested. Five native suites, sanitizer and JVM/Kotlin checks pass.
**No live graded preview/export yet**: active grades block export rather than drop the
look. Older apps cannot open newly saved v2 files. Android acceptance/new APK pending.
See [ADR 0017](docs/DECISIONS/0017-per-clip-grading-and-project-v2.md).

**Active plan: color grading first**, per user direction. See the
[পূর্ণ বাংলা ধাপভিত্তিক তালিকা](docs/IMPLEMENTATION_PHASES_BN.md).
Phase 1A now implements a real C++20 grading processor, RGB/luminance histograms,
versioned presets and a narrow Kotlin/JNI direct-buffer adapter. Production
Kotlin→JNI→C++ pixel checks, four native suites and sanitizers pass. **Not yet live
preview/export or saved clip grading; no new APK.** [Color contract](docs/COLOR_PIPELINE.md)
and [latest evidence](docs/PROGRESS.md) distinguish implemented and pending work.

**Next implementation batch, 2026-10-23:** safe native lifetime handles, exact-revision
JNI pages, immutable Kotlin snapshot cache and indexed viewport drawing. Native,
sanitizer and real JVM checks pass; production pure Kotlin cache/index also compiles
with pinned 2.3.21 and passes host tests. Full Android build/phone acceptance remains
pending; no new APK. See ADR 0015 and latest PROGRESS for evidence and remaining scope.

**2026-10-23 implementation batch (Android acceptance pending):** checkpoint receipts
and safe explicit repair; future-format preservation; derived Sources browser; UUID
project IDs; Bengali/emoji-safe JNI. Latest native/host JVM/sanitizer/resource checks pass.
Android compilation and phone testing for this batch are not yet completed; no new APK.
Default CI now uses fast build/static checks; emulator is manual opt-in and phone testing
belongs to the user. Approved-signer update requirements remain unchanged. Roadmap credit
is **25% complete / 75% remaining**, not a claim the advanced editor is finished.
See [latest evidence](docs/PROGRESS.md), [build instructions](docs/BUILD_AND_TEST.md)
and [update policy](docs/UPDATE_APKS.md).

The Android shell imports media through SAF, displays and plays a video preview, and exposes a timeline with split/trim/delete/ripple/undo/redo and newly authored same-track drag/reorder (pending Android validation). The user has now confirmed on a Galaxy F23 (Android 14, 6 GB RAM, Snapdragon 750G) that preview is visible and the editing features work; the attached screenshot shows a 3840×1632 source and timeline. This is a user-reported smoke test, not independent codec, A/V-sync, or export validation.

This checkpoint also adds project Save As/Open through the Android Storage Access Framework. A C++20 engine owns rational-time timeline/history and versioned project serialization. Android build/lint and APK alignment passed for the latest release, while emulator instrumentation failed; see [progress](docs/PROGRESS.md) and [build/test](docs/BUILD_AND_TEST.md) for exact limitations.

Owncut remains an early editing shell, not a complete professional editor. Preview still uses platform MediaPlayer clip handoffs rather than one timeline-driven composition graph. A restricted keyframe-aligned MP4 stream-copy exporter is in the current debug APK. User testing exposed frame-duration rounding rejection, empty gaps, and multi-source projects. The follow-up source work accepts half-frame rounding, explicitly compacts gaps after confirmation, joins multiple sources only when their codec/configuration/track formats match, and validates the muxed MP4 by reopening it and decoding its first frame before SAF copy. This new work is not yet Android-built or device-tested; current APK `phone-test-39270709637` predates it. General frame rendering/re-encoding, black-frame gap rendering, robust multi-track scheduling, write-ahead crash journaling, and production audio/color/effects are not implemented. A selected-clip media relink action is in the newest debug APK but awaits phone validation. Linux/Android local project replacements now call fsync; this is not a journal or power-loss guarantee. Project file Save As/Open does not rewrite original media.

## Build and test

Host core: a C++20 compiler (GCC or Clang); fast check: `make test`. CMake 3.20+ is supported. Android setup and commands use the pinned Gradle wrapper; see [Build and test](docs/BUILD_AND_TEST.md).

Read [the product specification](docs/PRODUCT_SPEC.md), [architecture](docs/ARCHITECTURE.md), [roadmap](docs/ROADMAP.md), [project format](docs/PROJECT_FORMAT.md), and [progress](docs/PROGRESS.md) before treating planned work as implemented.

### Gallery publication follow-up

Android 10+ export saves validated MP4s automatically to `Movies/Owncut` using a pending MediaStore row; no document picker is shown. Android 8–9 retain the SAF picker. Failed/cancelled copies attempt cleanup; provider deletion may fail. Existing empty files are not automatically removed. Device checks must cover successful Gallery thumbnail/playback, cancellation, unsupported-keyframe/format rejection without a new Gallery item, and storage/write failure cleanup. This does not add re-encoding or `.ccraw` decoding; the custom RAW format specification and sample footage are still required. Android/device verification of this publication change is pending.

## Basic render checkpoint (2026-10-21, emulator verified)

Default export now routes through `TimelineRenderExporter` (Media3 1.8.1) instead of
stream-copy. The native timeline snapshot supplies clip order and source trim ranges;
Android decodes/renders/re-encodes H.264 + AAC, source video cadence, 720-high SDR, preserving aspect
ratio. A silent audio track is generated where necessary. Gap removal still needs
confirmation; overlaps/multitrack export remain rejected. Import/play/edit are existing
features; RAW and grading are explicitly deferred until basic acceptance is complete.

New real-media instrumentation covers untouched input, non-keyframe trim, three clips
with different resolutions/aspects and a silent source, duration, output first/last
frames and pre-start cancellation. Synthetic fixture provenance is in androidTest/assets.
Run `39603413359` executed the instrumentation suite successfully, including the
new real-media render cases. Galaxy F23 playback/A/V sync, mid-render cancellation,
Gallery thumbnails and lifecycle still need physical-device verification. APK publication
is gated after emulator instrumentation and `phone-test-39603413359` is published; do not distribute the prior remux APK as this
render checkpoint. Earlier remux-only descriptions above are historical, not the new
selected UI path.

## Phone-confirmed basic render checkpoint (2026-10-21)

User reports successful import of about 5–6 videos, split/edit, removal of empty
timeline ranges, rendering and playback of the 720p result using the latest supplied
APK. This is a successful physical-phone user smoke test, not an independent sync
or performance measurement. Roadmap hardening, advanced tools, grading and RAW
remain incomplete. No new feature APK is produced by this documentation checkpoint.

## Basic reliability milestone (emulator regression verified)

Next scope selected by the user: basic editor hardening, not grading/RAW or MVP tools.
New source guards grouped-source relink durations, blocks edits during project I/O,
retains editor history/selection on export-time recreation while cancelling the old
render, preserves paused preview position and clamps seeks to the selected clip.
Gallery publication now uses a testable pending-row transaction with copy/close/size/
publish checks and explicit cleanup-failure reporting. Output duration and first/last
frame decoding are checked before publication. Android run `39620910796` executed the expanded JNI, real-codec, Activity
recreation, MediaStore and injected storage-failure suite successfully after the
initial regression failures were fixed. Build/lint and 16 KB alignment also passed.
Published APK: `phone-test-39620910796` from `4fc4590` (6,809,848 bytes).

Phone checks still needed: exact A/V sync, permission revocation/provider save/reopen,
low storage, Samsung lifecycle/process death. SAVE/LOAD/RECOVER-time recreation still
uses explicit autosave recovery, not background operation handoff. No provider-atomic
replacement, write-ahead journal, full shared preview/export graph, multitrack, grading
or RAW claim is added. Existing user-confirmed 5–6-clip 720p rendering remains valid.

### Reliability checkpoint results

Media import now participates in C++ AddClipCommand Undo/Redo history, including
last-import undo clearing preview. MP4 validation iterates by sample-track index so
negative AAC pre-roll timestamps are not mistaken for EOF. Regression tests were
not weakened after the first failing run. New hardening has emulator evidence, not
a new Galaxy F23 report; that phone's successful 5–6-clip render report concerns the
previous basic-render APK. Project schema is unchanged and reopen resets transient
history, as before. Save important work as a project document before changing debug
APK installations; do not delete app data to resolve installation errors without backup.

## Timeline interaction checkpoint — verified emulator batch (2026-10-22)

User reports the basic-hardening APK works correctly on their phone, with some
unspecified remaining issues. This requested batch adds an original red timeline
playhead, HH:MM:SS:FF ruler/time label, unified lane scrolling, tap-to-seek, marker-head
scrubbing, follow toggle/manual browse, proportional short clips/adaptive long scale,
source/sequence time separation and Undo anchor preservation. Native C++ owns point
lookup, exclusive clip boundaries and gap/overlap validation. Android preview now
requests closest-frame seeks and waits for initial seek completion before playback.
No project schema/output-preset/new dependency or advanced-tool/RAW claim is added.

See DECISIONS/0008 for research, clock/conversion/interaction details and test scope.
Host `make test` and `git diff --check` passed. Final code `8f5df51` passed
portable CI `39728248775` and Android CI `39728248796` (job `119657088154`):
APK build/lint, AndroidTest compilation, 16 KB alignment and the full Android 35
x86_64 emulator suite, including the new Canvas/touch/JNI/real-MediaPlayer seek,
playback advancement, split/Undo and non-selected clip move/source checks. Existing
render, publication, project-I/O and lifecycle tests also passed.
Phone-test prerelease: `phone-test-39728248796`, target
`8f5df51622fec4bd29542921875b330905c475b9`, APK 6,834,920 bytes.
This is emulator evidence, not a new physical-phone validation report.
Device checks still needed: red-marker smoothness/latency, fast repeated seeks, long
clip/zoomed timeline ergonomics, TalkBack/large fonts, source codecs and exact A/V sync.
The prior export gap-confirmation and single-track render limitations remain.


[Previous timeline-only APK (superseded by the workspace batch)](https://github.com/islamyousuf032-glitch/New-cc/releases/download/phone-test-39728248796/app-debug.apk)
(debug prerelease; back up saved projects before changing installations).


## Studio UI / Bengali-English checkpoint (2026-10-22)

User reports the project-workspace APK works well on their phone. This batch adds
an original slate/blue studio theme, card-based library, icon-only tool/navigation
actions with localized tooltips/accessibility, timeline lane/grid/clip polish, and
first-run English/Bengali selection persisted independently of project data. Home
Settings can change language. 256 default/Bengali resource pairs cover app-authored
menus, dialogs, statuses, export/recovery/errors; user names/media paths and native
diagnostics remain untouched. ADR 0010 records scope and platform references.

No new dependency, project schema, editing semantics or render preset. Current Flow
is still read-only. Host `make test`, resource/format/reference/contrast checks and
`git diff --check` passed; Android execution passed as recorded below.
This new visual design has not been visually inspected on a physical phone. Large
fonts, icon discovery, TalkBack, portrait/landscape and both-language provider flows
need device validation; do not claim host tests prove those.


### Studio UI batch — executed evidence

Final UI source `1e701d4b08bfcc71811fe54865101ce64db8098c`: Android run
`39764813788`, job `119777525804`, passed resource gate, APK build/lint,
AndroidTest compilation, alignment and full Android 35 x86_64 emulator regression.
New tests passed: mandatory first-run choice, Bengali context after recreation,
real Settings dialog selection back to English, preference retention, both-language
editor icon semantics/tooltips/minimum targets, icon bitmap unchanged by semantic
label length, and page switching without native timeline mutation. Existing project,
playhead, JNI, lifecycle, real-media render and publication regressions passed too.
Portable run `39764813603` passed resource gate, CMake configure/build and CTest.
Local `make test`, Python resource/reference/placeholder/contrast gate (256 pairs)
and diff check passed. No physical-phone visual/TalkBack/ergonomic validation yet.

Prerelease `phone-test-39764813788` targets that exact source; APK 7,018,288 bytes
and SHA-256 asset were verified as uploaded via release metadata. Optional Actions
report upload still exceeded storage quota: no independently downloaded XML/lint
report or full-screen screenshot review is claimed. Explicit CI step outcomes were
checked. Documentation-only follow-up does not change the tested APK implementation.


## Quiet editor / theme checkpoint (2026-10-22)

User phone screenshots show the studio-icon build works but still looks cluttered.
ADR 0011 records screenshot-driven hierarchy changes and official color-role research.
A fixed portrait editor prioritizes fitted viewer, transport, timeline and one focused
edit row; metadata/source seek, help and secondary/project tools are contextual.
Small-height/large-font windows retain a scroll fallback. Three persisted role-based
themes (Graphite, Midnight, Ember) share palette JSON and contrast tests. Home Settings
selects language or Appearance. Existing processing/render behavior is unchanged; Flow
is read-only. Quiet status retains important-failure/recovery notifications.

Resource/three-palette contrast checks and host tests are run below; Android layout/
theme/full regression and actual-phone aesthetic approval are separate gates. No new
dependency, proprietary assets, project migration, grade or node renderer added.


### Quiet editor / themes — verified execution

Code `cb5bfa7cc476b435f3fa6c35b68bcec0302b5a7e` passed Android run
`39777165014` / job `119818925802`: bilingual/three-theme contrast gate, build/lint,
AndroidTest compilation, APK alignment, and full Android 35 x86_64 emulator suite.
New tests verify quiet default Edit (no metadata/source slider/status/log toolbar),
one visible Play/Import, contextual Media/Tools, same viewer/timeline instances,
three persisted/recreated themes, and important failure notification deduplication.
Existing language/icon, playhead/real seek, history/lifecycle, project/library, render
and publication regression tests passed. Portable CMake/build/CTest run `39777165246`
passed. Local `make test`, 261-pair resource/contrast gate and diff check passed.

Verified prerelease `phone-test-39777165014` targets that exact source; uploaded APK
7,021,315 bytes plus SHA-256 asset. Optional Actions report upload hit storage quota;
step outcomes and release metadata were checked, not independently downloaded XML
or full-screen screenshots. Actual aesthetic approval, TalkBack, user font overrides,
short-window/large-font scrolling and phone codecs remain phone-validation tasks.
No claim that automated layout checks certify professional appearance.


## Phase B storage and install-over delivery checkpoint (2026-10-22)

User accepts quiet UI and requests planned substantive work, progress percentages and
full APK updates over the existing app. ADR 0012 and UPDATE_APKS define the delivery
contract and old-key limitation. New managed store owns a cooperative writer lease,
retained across rotation, checks bounded file fingerprints before native checkpoint,
validates recovery before replacing state, and guards catalog repair with metadata
archives. External Save As reports managed-primary conflict separately. Native edit
math/project schema unchanged; full coordinator/journal and node evaluator remain future.

CI now tests N−1→N install-over with real native project and actual app preferences,
and publishes only when durable signing + pinned certificate configuration is present.
Legacy random-debug-key compatibility is not promised. GitHub secret management
returned 403; owner-side setup is required, never passwords or keys in chat.

Readiness ledger v1 is a weighted scope estimate: baseline 25% credited, 75% remaining.
New managed storage 3 points remains pending Android gate; signing delivery is blocked,
not credited. Local core/resource, 7 mocked signer/manifest/reference-verifier scenarios and
3 emulator-reset refusal tests passed (metadata scenarios also with Python -O).
At source 8a6aab4, Android 39796552264/job119885073164 passed build/lint/alignment
and the full Android 35 x86_64 suite, but the separate install-over gate failed.
Its initial annotations were truncated by GitHub; diagnosis is not resolved.
Source a1bb412 Android 39799182223 failed before job startup: GitHub reports failed
account payment or spending-limit exhaustion. Latest diagnostic/reference-signer
and CI-only-emulator guard changes are host/syntax tested, not emulator verified.
No new APK was published; stable signing is not configured, and first-patch signer
must also match approved phone-test-39777165014. Do not uninstall/clear user data.


### Native session coordinator checkpoint — 2026-10-22

Phase B now extracts native project/history ownership into C++20 SessionCoordinator
and routes JNI operations through it. Revision-checked 256-entry pages, failure
rollback, explicit save-copy/draft-restore and portable concurrency/recovery tests
are included. See ADR 0013 for contracts and limits. Host core/session tests, strict
CMake/CTest, ASan+UBSan and host JNI-header syntax checks passed. New Android bridge
tests are added but unrun; Activity orchestration and durable journal remain pending.
No graph evaluation, UI-memory virtualization, Android ABI/device verification or
install-over success is claimed. Readiness stays 25% credited / 75% remaining.
APK publication remains gated by UPDATE_APKS; earlier delivery blockers are not fixed
by this native refactor.
