# Roadmap and acceptance gates

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

Separate fresh-install test delivery is now authorized; stable update route preserved.
Publication/phone acceptance pending, no feature or readiness credit (ADR0028).


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


## APK infrastructure recovery (2026-10-24)

Manual alternate-runner and pre-build signer checks implemented. Owner provisioning or
billing correction, usable original key, Android compile/package and user phone acceptance
remain open. No phase/readiness credit or new APK (ADR0026; recovery guide available).


## Phase1C draft iteration (2026-10-24)

Unsaved draft-to-live preview is wired without repeated Apply/history commands; stale
still feedback and child-player ownership implemented. Native-host coverage passes, but
Android build, phone playback/export parity and update-compatible APK remain open.
No readiness credit or export guard removal (ADR0025).


## Phase1C preview usability hardening (2026-10-24)

Terminal resource ownership, buffering/seek controls, accessible progress input and bounded
still requests are implemented with host policy coverage. Actual Android lifecycle/audio/
accessibility/rotated-frame behavior and preview/export parity remain acceptance gates.
No guard removal, APK or readiness credit;25%/75% (ADR0024).


## Phase1C output-verification groundwork (2026-10-24)

Native frame-comparison metrics pass native/JNI/Kotlin checks. Actual fixture/candidate
encode/decode test sources now target clip-specific exposure, bypass and cut margins;
Android compilation/execution/tolerance validation remain open. No encoded-output/live
parity or release claim, no guard removal; readiness stays25%/75% (ADR0023).


## Phase1C experimental playback connection (2026-10-24)

Opt-in clip player source now connects native recipe + explicit shared GPU factory +
raw decoded format gate; clip loop/seek/audio/Before/release are implemented, not Android-
validated. Native/host tests pass; actual renderer/codec/GL/thread/pixel and timeline/export
acceptance remain open. No guard removal/APK or new credit;25%/75% (ADR0022).


## User-visible Color batch (2026-10-23)

Source implementation now includes17controls, actual native CPU still preview, Before/After,
Apply/history/autosave and bilingual draft input. Real candidate encoder runner entry added;
normal export remains guarded. Android compilation/still UI/codec output/live preview and
APK are not verified. No complete1C/1D claim, credit remains25%/75% (ADR0021).


## Phase1C decoded boundary checkpoint (2026-10-23)

Native decoded-format verifier and terminal per-render gate pass host/JNI/Kotlin tests;
candidate decoder-factory source now connects it before both surface release overloads.
No live preview/shipping graded export. Android compilation, actual codec/owner-thread/
pixel/source identity and shared preview integration remain open. CI39958756231 did not
start due to billing/spending limits. No new credit, stays25%/75% (ADR0020).


## Phase 1C next checkpoint (2026-10-23)

Raw source probing/native color admission, immutable shared color plan and candidate
per-clip Media3 composition are implemented in source; native/JNI/pure-Kotlin tests pass.
Android compilation/probe/codec tests and live preview/export connection are still open.
Do not count composition construction as encoded export. Preserve guards, gap consent
and user-owned phone testing. ADR0019; readiness stays25%/75%.


## Phase 1C started (2026-10-23; shared foundation only)

Native explicit texture profiles, original shared GLSL and matched CPU oracle now pass
six suites and software GLES/reference checks (96 cases, <=1/255 observed). A candidate
Media3 SDR adapter is added but unconnected/uncompiled on Android. Still pending: source
metadata extraction, shared live preview/render path, decoded encoded-output comparisons,
seek/cancel/lifecycle and phone acceptance. Guards remain; do not credit full 1C or phase 1.
See ADR 0018. Verified roadmap remains **25% complete / 75% remaining**.


## Phase 1B checkpoint (2026-10-23)

Native clip-owned grades, grouped history, v2 persistence/v0-v1 migration and grade-aware
immutable snapshots are implemented and host-tested. Android managed-grade tests are
written, not compiled/executed. Next is **phase 1C shared color-managed preview/export**,
then live controls/scopes; do not count blocked grade export as a delivered effect.
Verified credit remains 25/75. ADR 0017 defines backward-reading compatibility and the
explicit no-silent-drop export guard until rendering integration is verified.

## User-prioritized sequence — color first (2026-10-23)

Canonical remaining-work breakdown: [বাংলা implementation phases](IMPLEMENTATION_PHASES_BN.md),
ADR 0016. Order: **1 color grading**, 2 editor/project/media reliability, 3 real nodes,
4 compositing, 5 animation/motion, 6 audio, 7 advanced color/HDR/performance, 8 camera/
.ccraw specification and implementation, 9 compatible update/release completion.
Safety fixes necessary for a current feature are not postponed merely by this order.

Phase 1A native SDR grading/reference/frame bridge/histogram/preset is implemented and
host-tested. Phase 1B persisted clip grades/history is next, then shared GPU preview/
export, controls/scopes and color parity acceptance. No live grading or full phase-one
completion is claimed. Color ledger is in_progress with **25% total credit / 75% remaining**.

## Next implementation checkpoint — safe lifetime/projections (2026-10-23)

Native opaque handles with in-flight lifetime pinning, exact revision-page JNI, immutable
Kotlin revision cache and indexed viewport draw culling are implemented and host-tested.
Production pure Kotlin cache/index now has real compiler/JVM checks in addition to C++
and JNI tests. These do not close busy-Activity recreation/controller ownership, full
UI virtualization or the Android gate. Graph evaluation and advanced roadmap remain open.
Readiness remains **25% credited / 75% remaining**, not elapsed implementation effort.

## Active batch checkpoint — 2026-10-23

Implemented and host-tested: diagnostic checkpoint receipts/shared atomic storage,
interruption hooks, future-format preservation, explicit receipt repair, derived Sources
browser, UUID identities and standard-Unicode JNI. Android tests are written, not yet
compiled/executed for this batch. This does **not** close the write-ahead journal,
power-loss, scalable asset registry, controller-lifecycle or Android acceptance gates.

Next integrated work: controller/lifecycle ownership and bounded UI snapshots; then a
persisted typed graph with real preview/export evaluation before editable node controls.
Advanced grading, RAW/camera `.ccraw` specification and the remaining forward roadmap
are not implemented. No decorative Flow editing is counted as engine progress.

Readiness: **25% credited / 75% remaining**; no new device-verified credit. CI's default
handoff is now a statically checked signed candidate, with optional manual emulator
checks; this supersedes older mandatory-emulator publication wording, not product
acceptance criteria. Phone testing remains with the user. See ADR 0014 / UPDATE_APKS.

## Phase 0 — Foundation (implemented with remaining hardening)

- [x] Repository/environment audit recorded.
- [x] Portable C++20 rational-time/timeline foundation, edit-command/history, project codec/store, host tests, CMake and host CI.
- [x] Kotlin Android shell, pinned Gradle/AGP/Kotlin/SDK/NDK/CMake, owned JNI session, Android build/lint/alignment workflow, and instrumentation sources.
- [x] Versioned project schema (v1) and v0 migration reader; move/split/trim/delete/single-track ripple-delete commands with undo/redo.
- [x] C++ autosave snapshot API; debounced Activity autosave, best-effort shutdown save, and explicit recovery confirmation.
- [x] SAF video import and Android-visible preview/editing shell. Galaxy F23 user reports playback and editing features work; see device matrix for the scope of that evidence.
- [x] App-private project save/load and SAF project-document Save As/Open adapter; Android build/lint/AndroidTest-source compilation passed in run `39247694047` (real provider test still pending).
- [x] Linux/Android local project replace now syncs temporary file and parent directory after rename.
- [ ] Write-ahead crash-recovery journal, provider-safe atomic replacement, power-loss fault injection, broad instrumentation and recovery failure tests.

Gate: host tests pass; Android debug build/lint/alignment pass; instrumentation must execute successfully; device checks and reproducible instructions are documented. Android run `39603413359` passed build/lint/alignment and executed emulator instrumentation. Galaxy F23 basic rendering is now user-confirmed; recovery, lifecycle and broader failure-mode gates remain open.

## Phase 1 — End-to-end vertical slice (partial; acceptance gate not met)

- [x] SAF media picker, metadata extraction, visible platform preview/playback surface, seek controls, JNI media-reference insertion, and user-visible split/trim/delete/history controls.
- [x] Per-track timeline blocks, selected clip, playhead, background project save/load, autosave/recovery, and initial playback across directly adjacent same-track clips.
- [x] Galaxy F23 user smoke test reports visible video and working editing controls. The later basic-render phone test also confirms edited export playback (see below), but does not establish measured A/V sync, broad 4K/codec support or save/reopen correctness.
- [x] SAF project-document source compiles in Android build and its bounded-copy instrumentation source compiles; [ ] execute test on emulator and real Documents-provider save/reopen/revocation tests.
- [ ] One timeline-driven preview and export graph, multitrack A/V scheduling, gap handling and sync validation.
- [x] Replace remux-only UI export with decode/render/re-encode for arbitrary source trims and sequential mixed sources; emulator codec cases passed and Galaxy F23 user confirms 5–6 imported videos, split/edit, confirmed gap removal and playable 720p export.
- [ ] Verify mid-render cancellation, low-storage/provider cleanup and measured audio continuity on a physical phone.
- [x] Basic single-track decoder/render/re-encoder export for arbitrary frame cuts and sequential multiple sources.
- [ ] Render black gaps without compaction, shared preview/export graph, multitrack composition and cross-track audio scheduling/mixing; measured A/V sync validation.
- [ ] Android build/device acceptance for same-track timeline drag/reorder and media relink, including the new group-relink command; cross-track movement, robust recovery and lifecycle/surface-recreation test coverage.

Acceptance: meet Android/device criteria in `PRODUCT_SPEC.md`, including same timeline for preview/export, persistence, A/V sync, error/cancel and lifecycle tests. Basic single-track export is implemented and user-tested; the broader Phase 1 acceptance gate is not yet complete.

## Phase 2 — Usable MVP

Multiple clips/tracks, thumbnails/waveforms, multi-track-aware ripple semantics, basic transforms/text/transitions/audio mixing, autosave and robust cancellable export. Require deterministic command round trips, serialization/migration/recovery tests, render comparisons and device profiling before gate.

## Phase 3 — Advanced editing

Advanced trims, keyframes/curves, speed control, proxies and measured cache/playback optimization. Gate on edit invariant tests and performance/memory budgets on the device matrix.

## Phase 4 — Color, audio and effects

Explicit color management, scopes, curves/LUT, selected effects/masks, and advanced DSP. Gate each feature on test vectors, output tolerances and device capability validation; no HDR claim without complete pipeline evidence.

## Phase 5 — Advanced/AI

Selected tracking, stabilization, subtitles and AI only after consent, on-device privacy, model license/size/RAM/acceleration and offline behavior review. HDR/high-resolution remains capability-gated.

## Phase 6 — Release hardening

Device matrix, profiling, accessibility, privacy/store checklist, dependency/license review, release signing instructions and legal review where needed.

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

## User-confirmed basic render acceptance (2026-10-21)

User installed the latest supplied basic-render APK and reports importing about 5–6
videos, splitting/editing them, removing empty timeline ranges with the render flow,
and successfully playing the rendered 720p video. This closes the basic sequential
import/play/edit/render smoke test on the previously identified Galaxy F23 only.
No source codec/resolution inventory, precise A/V sync or performance measurement,
mid-render cancellation, save/reopen, lifecycle or low-storage check was supplied.
Advanced tools, grading and RAW are not complete; the next implementation scope must
be an explicitly bounded milestone rather than declaring all phases complete.

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


## Project-workspace / node architecture baseline (2026-10-22)

The user reports the playhead APK works well on their phone; this is user smoke
evidence, not an independent sync/performance benchmark. [Advanced architecture v1](ADVANCED_ARCHITECTURE.md)
and ADR 0009 now define the canonical forward design; earlier checkpoint descriptions
above are historical. The baseline distinguishes timeline editorial time, typed node
processing, contextual workspace UI, managed persistence and future shared evaluation.

This implementation adds a launcher project library (create/name/import/list/open),
per-project native save/recovery, non-destructive legacy workspace discovery, managed
checkpoint on Projects/Back, retained workspace pages and Edit/Media/Deliver panels.
Flow is a read-only explanation of the current separate preview/export routes. Native
`engine/graph` validates a bounded typed output DAG but has no renderer, JNI or file
schema integration. No advanced grading/audio/RAW/compositor or output-preset claim.

Host tests and Android verification for this batch are recorded in the final evidence
entry below when executed. Physical-phone verification of this new navigation is pending.
Test on phone: create two projects, import/edit/back/reopen isolation, external Save As,
legacy recovery, large fonts/rotation, media relink, render/Gallery, and Back during I/O.


### Executed architecture-batch evidence

Final implementation `f652c381316d209eb6be2bb5b736c89ede802e12` passed Android
run `39748116540` / job `119721585167`: APK build/lint, AndroidTest compilation,
16 KB alignment, KVM and full Android 35 x86_64 instrumentation. New catalog tests
cover isolated documents, names/path validation, non-destructive/idempotent legacy
migration, malformed/oversized metadata with valid cards preserved, launcher routing,
managed checkpoint/reopen and retained workspace page without replacing the viewer.
Existing playhead/JNI/lifecycle/project-I/O/render/publication regressions also passed.
Portable CMake/build/CTest run `39748116580` passed. Local `make test` and a strict-warning
ASan/UBSan execution of the unchanged native engine sources passed. Local CMake was
not installed; do not confuse CI execution with a local run.

Verified published prerelease `phone-test-39748116540` targets the tested source;
APK `app-debug.apk` is 6,867,764 bytes and checksum asset is uploaded. Actions' optional
report artifact upload still exceeded storage quota; step outcomes/release metadata
were checked, but downloadable XML/lint reports were not independently inspected.
This is emulator/host evidence; physical-phone validation of the new library/navigation
is pending. The user's latest successful phone report refers to the earlier playhead
APK, not this architecture batch. Later documentation-only changes do not alter APK code.


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
