# Product specification

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

User explicitly accepts uninstall/fresh install for the current test candidate. Warn of
app-local data loss and back up projects/media; temporary debug signer does not promise
future updates. Do not describe this as an update patch or finished grading (ADR0028).


## Current build checkpoint (2026-10-24)

Actual Android assembly/lint/test-source compilation/alignment passes; this is not phone
acceptance or a downloadable compatible update. Stable signing remains required before
candidate publication; shipping grade-export guards remain intact. See ADR0027.


## APK delivery recovery (2026-10-24)

Owner billing correction or trusted alternate runner is needed to start blocked builds.
Original signing identity remains mandatory; alternate compute does not fix key loss.
No uninstall/data-clear fallback. New runner/preflight source is not delivered APK or
phone acceptance; actual build/signing still pending (ADR0026).


## Experimental draft playback (2026-10-24)

Color editor can open experimental playback of its current draft without saving.
Apply remains a separate validated-still action. Edited values visibly invalidate
the still/Apply; closing the editor also closes its preview. Source/session changes
reject stale opening/commit. Implemented source only; Android acceptance pending.


## Experimental preview behavior (2026-10-24)

Failure/close must disable controls and release the isolated player; late events cannot
restart it. Buffering can pause, but cannot seek/compare. Touch seeking preserves thumb
control; keyboard/accessibility inputs also seek. Still preview requests a bounded proxy;
Android8.0 refuses oversized sources with explanation (Android8.1+ uses scaled extraction).
These are implemented source, not phone acceptance. Live/export parity and update-compatible
APK delivery remain open; shipping grade-export guards stay (ADR0024).


## Encoded grade acceptance test source (2026-10-24)

Native frame diagnostics are implemented/host-tested. Device test source now compares
actual neutral/graded/bypass candidate encodes at clip interiors and cut margins. Lossy
thresholds are proposed, not measured results; gray sampling is not full-color or live
preview acceptance. Android/phone/APK tests remain open; guards unchanged (ADR0023).


## Experimental live clip preview source (2026-10-24)

Color can explicitly open one saved clip snapshot in a graded video player, trim-bounded
loop/seek/pause/audio, Before while playing. It is experimental and Android-unverified,
not normal timeline integration or user graded export. Fail closed on unsupported raw
output; close/background release. No unsaved draft, model edits, full grading/APK/parity
claim. Keep shipping export guards until actual acceptance (ADR0022).


## Color editor source implementation (2026-10-23)

Color workspace:17parameter draft, native still Render, Before/After, enable/reset, explicit
Apply/Cancel with history/recovery. Require known SDR source + sRGB bitmap; reject unknown
rather than fake processing. Clearly label still-only/ungraded playback/guarded export.
This is not live preview/full grading completion. Candidate encoder entry is test-only;
Android app/UI/codec acceptance and compatible APK are still pending (ADR0021).


## Candidate decoder gate (2026-10-23; not released)

Require known admitted SDR output tags and unchanged coded geometry/range before every
rendered release; fail closed on missing/changed metadata or query failure. This strict
subset can refuse valid decoder normalization until tested. Native/host checks pass,
Android/codec/preview/export acceptance does not. No URI identity or pixel correctness
claim, no guard removal/new APK; see ADR0020.


## Phase 1C source admission/candidate (2026-10-23)

Initial supported candidate subset requires raw explicit Rec709 + SDR-video + full/limited
range and no HDR-static metadata; unknown/HDR/ambiguous tracks refuse without fallback.
Local source probing and immutable per-clip grade-plan source are implemented; Android
probe/codec behavior is unverified. Candidate composition is not shipping graded export
or preview. Keep explicit gap-compaction consent, existing guards and unchanged neutral
behavior. No source-byte freshness, realtime, phone or new APK claim (ADR0019).


## Phase 1C foundation (2026-10-23; not a released grading feature)

Shared native texture/shader contracts and software GPU/reference checks are implemented;
candidate Media3 adapter is unconnected and Android-unverified. Initially accept only
explicit BT709 SDR source metadata with known range; refuse unknown/PQ/HLG/other-primary
rather than guess. Source metadata extraction, live preview/export connection and parity
are still required. Keep current ungraded preview warning and active-grade export refusal.
GPU float limits must reject unsupported grades, never silently remove the saved look.
No new controls/APK or device/performance claim; phase 1C is incomplete (ADR 0018).


## Phase 1B implementation (2026-10-23; Android acceptance pending)

Per-clip grade data, grouped set/reset/paste, bypass and native undo/redo now exist.
Grades persist through project v2, checkpoints/recovery and copy; v0/v1 import neutral.
Reopen preserves grades, not undo history. Split inherits; other edits preserve grades.
No live Color controls/graded preview/export yet. Until phase 1C, active grades block
export and selected-clip information explicitly identifies ungraded preview. Older
apps cannot open v2 documents. These internal APIs do not complete phase-one acceptance.

## First advanced feature: color grading (2026-10-23)

The user-selected first phase is color grading, followed by the explicit phases in
IMPLEMENTATION_PHASES_BN. Core math/validation/scopes/preset logic is C++20; Kotlin is a
platform/UI adapter. Phase 1A native frame processing and its narrow JNI adapter now
work in host tests. This is **not** a live Android grading UI or graded video export.

Phase-one acceptance: grade binds to stable clips, survives save/recovery, supports
undo/reset/bypass, and produces consistent color-managed preview and actual encoded
output with usable bilingual/theme-aware controls and scopes. Current preset support
is standalone, not project persistence. No unsupported HDR/transfer/white-balance claims.

## Timeline/lifetime batch (2026-10-23; host evidence only)

Unchanged timeline revisions reuse immutable snapshots; changed revisions arrive through
256-entry checked native pages. Viewport interval projections avoid drawing offscreen
clips, including correct dragged-clip inclusion. Native invalid/stale handles are rejected
rather than dereferenced. This is not a complete scalable asset system or virtualized
editor. Full Android acceptance and user phone testing remain required; Flow is read-only.

## Current recovery/sources implementation (2026-10-23; Android acceptance pending)

Managed project saves checkpoint recovery and primary with diagnostic receipts. The
Tools repair action requires confirmation, preserves damaged receipt bytes and recovery,
and refuses unsupported future formats or filesystem failures. Sources groups only
media already referenced by clips, pages 20 entries, and selects an associated earliest
clip; it is not a complete asset-bin/import-management system. New sessions use UUIDs,
and the JNI boundary supports Bengali and supplementary Unicode such as emoji.

Acceptance still requires Android build and user phone checks of save/reopen/recovery,
source selection and Unicode. Host tests do not establish UI, codec or update behavior.
Phone testing is user-owned; automatic CI device runs are optional (ADR 0014).

## Vision

A touch-first, offline-first, non-destructive Android video editor with a distinct identity and professional editing depth. Cloud accounts and AI services are never required for core editing. Original media remains untouched.

## Phase 0 acceptance criteria

- A portable C++20 core builds independently of Android.
- Time is represented using integer ticks and explicit rational timebases.
- Timeline model accepts multiple typed tracks and media-referencing clips, validating identifiers, source ranges, and locked tracks.
- Host unit tests cover rational frame-rate comparison/rescaling and core validation.
- Android debug APK builds for arm64-v8a and x86_64 with the pinned toolchain.
- Kotlin creates/destroys one native engine session and batches user-command/project calls; instrumentation covers edit/history/project/autosave JNI round trips.
- Build, test, architecture, licensing, and limitations are documented.

Portable requirements have host tests. Android APK build/lint and alignment pass in recent CI; instrumentation execution has failed and is not accepted as passing.

## Phase 1 acceptance criteria (partially implemented)

Implemented: SAF video import, metadata read, visible MediaPlayer preview through TextureView, play/pause/seek, timeline clip selection and split/trim/delete/ripple/undo/redo, app-private save/load/autosave/recovery, and SAF project Save As/Open adapter. The SAF adapter was Android-built/linted and its instrumentation source compiled in run `39247694047`; real Documents-provider behavior remains untested. The user reports the preview and editing controls work on Galaxy F23 / Android 14; screenshot shows a visible frame and timeline for a 3840×1632 source. This is one user-reported phone smoke test only.

Not yet satisfied: one general timeline/render graph must drive preview and export; correct black-frame gap/transition rendering; robust cross-track A/V scheduling and sync; cancellation/failure cleanup and lifecycle/surface tests; broad edited output export on a supported device. A narrow lossless MP4 remux exporter is in APK `phone-test-39270709637` for keyframe-aligned cuts, frame-duration rounding and explicitly confirmed gap omission/clip concatenation. User screenshots additionally show multi-source limits and Gallery thumbnail failure. New source allows multiple URIs only with identical relevant codec/track configuration and validates the muxed MP4 plus first-frame decode before copy; this is not yet Android-built or device-tested. This is not black-frame rendering or general export. Current preview uses separate MediaPlayer clips and stops at timeline gaps. APK `phone-test-39250035336` includes same-track long-press drag/reorder with free-slot snapping through JNI; the new gesture awaits Galaxy F23 validation. Cross-track drag is absent. Project documents are copied through app-cache temporary files and provider-side atomic replacement is not guaranteed; Linux/Android local files now sync data+directory, but the write-ahead journal and power-loss fault tests remain absent. APK `phone-test-39250035336` includes selected-clip relink with source-range validation and undo. The newer source changes it to update every clip sharing the original media reference as one atomic undoable edit; that group behavior needs Android build and Galaxy F23 validation.

Acceptance gate: on documented supported Android devices, import a locally accessible SDR video, visibly render frames, play/pause/seek with corresponding audio, edit through the timeline that drives preview and export, save/reopen and recover safely, then export the edited result through a supported codec. Validate A/V sync, cancellation, failure cleanup, lifecycle/surface recreation, and output correctness. Host tests, metadata import, and user-reported playback alone do not satisfy the export/sync/persistence gate.

## Product constraints

Initial target is representative 1080p SDR, not universal 4K/60/HDR support. Unsupported media should produce actionable errors. HDR is not claimed without end-to-end precision, display, tone mapping, encoder, and metadata validation. Phone/tablet adaptive editing, accessibility, storage recovery, privacy, and thermal/memory budgets are release requirements.

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
