# Architecture index and execution history

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

Fresh-install debug candidate uses a separate explicit publication route and metadata;
stable update signing guards are unchanged. Validate actual APK identity before release.
No runtime architecture/color change (ADR0028).


## Current build checkpoint (2026-10-24)

Explicit highp sampler precision is part of the shared native shader contract, not just
float variables. Mesa host parity now passes unchanged gates. Android graph/test sources
compile; codec/live/display parity remains unrun. See ADR0027.


## Build/signing ownership (2026-10-24)

Manual trusted alternate runner is infrastructure transport, not application behavior.
Push/PR cannot route to it. Preflight compares only public certificate identity against
approved baseline/pin using existing private keystore locally; final APK verifier remains
authoritative. No source-level billing bypass or original-key regeneration (ADR0026).


## Draft preview ownership (2026-10-24)

ColorGradeDraft.validatedPreview parses immutable values and calls the native shared GPU
budget validator; no session writes. MainActivity validates current session/clip and
passes a copied snapshot to LiveColorPreview. Editor owns child dismissal; activity owns
background release. Saved gradeActive is not reinterpreted as draft color policy; the
isolated plan generates native shaders from grade values. Android validation pending.


## Preview transport ownership (2026-10-24)

Platform-only terminal state policy owns preview controls; failure clears player ownership
before releasing and cannot be reopened by late events. Scrubbing suspends thumb updates;
keyboard/accessibility changes use the same exclusive-end seek policy. CPU still requests
are metadata-oriented/no-upscale1280×720 on API27+, with oversized API26 refusal. Returned
bitmap validation does not establish source color parity or bound decoder internals.
Native C++ grading remains authoritative; Android validation pending (ADR0024).


## Native parity diagnostics (2026-10-24)

color::compare_rgba8 owns bounded read-only RGBA code-value diagnostics; narrow JNI/Kotlin
transports immutable metrics, no UI math duplication. Device-generated gray fixture and
actual candidate encode/decode test source check per-clip look/bypass across a cut using
a native CPU oracle. Host diagnostics pass; Android encoder/fixture tests uncompiled/unrun,
not calibrated/live-display parity. No runtime path/guard/schema change (ADR0023).


## Experimental clip playback graph (2026-10-24)

Color opt-in uses LiveColorPreview/GradedPreviewRenderers; it is NOT the timeline player.
Worker captures shared plan/recipe; renderer-owned graph uses SingleInputVideoGraph with
explicit DEFAULT processor factory. Native gate checks input/actual output on format
changes and actual output before non-decode-only buffer processing. Legacy MediaPlayer
stays paused/unchanged; ExoPlayer owns clip audio/surface/graph release. No model writes.
Close/background suppresses stale startup. Actual Android behavior unverified; ADR0022.


## Color editing surface and still-processing route (2026-10-23)

ClipColorEditor owns a worker/draft, metadata/frame extraction and explicit native-processed
sRGB still image. ColorFrameBuffers transports straight ARGB/RGBA only; ColorGradeDraft
parses bilingual text, not grading math. Apply is one SessionCoordinator grade command via
existing EngineSession plus Activity dirty/autosave; Cancel has no edit. Preview proxy scales
before grading and is not a video parity oracle. Candidate encoder now has an explicit test
entry into the existing worker; shipping guards remain. Android execution unverified (ADR0021).


## Candidate decoder boundary (2026-10-23)

C++ validates decoded coded geometry/color against the admitted input descriptor; JNI
transports a fixed12-int block. Codec-owner-thread DecoderColorGate revalidates every
surface render and latches failures/closure. GuardedDecoderFactory delegates audio and
intercepts both rendered-buffer release overloads; drops do not render. Candidate builder
uses DefaultAssetLoaderFactory + guarded decoder + explicit GL factory. No shipping route
calls it. Factory Format lacks URI, so descriptor matching is not source identity; crop/
rotation/pixels/thread behavior and actual Android operation remain unverified (ADR0020).


## Phase 1C source/plan boundary (2026-10-23; candidate)

C++ owns VideoColorMetadata admission; JNI canonical codes are independent of platform
constants. VideoSourceProbe transports raw local-source MediaExtractor tags, preserves
missing tags as unknown, refuses ambiguous video tracks and releases extraction ownership.
ColorRenderPlan owns detached clip/metadata/recipe snapshots, request-local dedup and
cancellation/failure publication. Candidate composition consumes exactly that plan,
per-clip grade then global Presentation, one explicit processor factory per plan. Shipping
paths still guard active grades and use MediaPlayer/legacy Transformer. No decoder proof
or source-byte pinning: revalidate live format before enabling. See ADR0019.


## Phase 1C GPU foundation (2026-10-23; candidate, unconnected)

color_gpu.hpp/.cpp owns explicit texture encodings, the double reference and immutable
GLES2 shader recipes. JNI transports ASCII sources; Kotlin holds no grade math. Media3
1.8.1 DEFAULT SDR is encoded SMPTE170M, not the GlEffect interface's stated linear space.
GradedFramePipeline therefore bundles an explicit DEFAULT factory with its effect and
rejects unsupported source ColorInfo/HDR. GL resource ownership stays on caller's GL
thread; source grade is captured immutably. Full float32 fragment capability and a native
safe intermediate budget are required. ADR 0018 defines profiles/rejection policy.
Software GLES/native reference tests pass, but no app playback/export route installs the
adapter. Source-media metadata extraction and shared connection remain next work; guards
stay. No live GPU/video acceptance or new APK is implied.


## Phase 1B data/history implementation (2026-10-23)

Clip owns color::Grade; C++ SetClipGradesCommand publishes grouped edits atomically and
captures old grade values for undo. Split/trim/relink/reorder/delete reuse clip values;
no URI-keyed grading map. ProjectCodec v2 embeds bounded grade payloads, migrating v0/v1
to neutral. Native revision pages expose exact settings and effective activity; Kotlin
snapshots/settings are immutable transports. Existing managed checkpoint/recovery paths
use the updated codec. See ADR 0017 for format and failure contracts. Preview/export
processing is still absent; active grades block export and preview copy warns clearly.

## Active implementation sequence: color first (2026-10-23)

Per user direction/ADR 0016, IMPLEMENTATION_PHASES_BN is the active phase order. First
implement genuine native grading, then per-clip/history persistence and shared preview/
export integration; controller work remains required but is no longer the first feature.
`engine/color` is a C++20 standard-library-only CPU reference/frame processor with
validated settings, RGBA8 safety, histograms and standalone preset serialization.
`color_jni.cpp` + ColorEngine.kt transport direct buffers/17 immutable settings only.
Production Kotlin/JNI/C++ pixel processing is host-tested; it is not yet connected to
MediaPlayer, Transformer, project files or Flow. COLOR_PIPELINE defines color semantics.

## Safe handles and timeline projections (2026-10-23)

ADR 0015 replaces JNI pointer handles with process-local SessionRegistry tokens. Each
call pins shared ownership, while close blocks new acquisitions without freeing an
in-flight session. Native snapshots now expose exact revision-string pages; Android's
EngineSession caches a complete immutable list by revision, replacing whole-JSON reads.
Native page indexing skips preceding track vectors. TimelineClipIndex culls draw candidates
by viewport and preserves painter order/dragged clips. Full list storage, hit testing and
Activity task/controller recreation ownership are not yet virtualized/extracted.

## Latest implementation — checkpoint/sources batch (2026-10-23)

`AtomicFileStore` centralizes temp-write/flush/fsync/rename/parent-sync.
`CheckpointJournal` adds bounded versioned diagnostic receipts beside unchanged v1
project/recovery files. `SessionCoordinator` serializes checkpoint, repair and derived
media catalog calls with edits; JNI remains a conversion boundary. Managed storage owns
writer leases/catalog and exact-model fingerprint reconciliation. This is not a single
transaction across native files and the Android catalog. Repair is explicit and archives
malformed receipts only; unknown versions and I/O errors are not repairable corruption.
See [ADR 0014](DECISIONS/0014-checkpoints-sources-and-phone-handoff.md).

Sources is derived from current timeline references, with 20-entry presentation pages;
full native JSON/UI snapshot materialization remains. UUID identity is supplied by Kotlin;
standard UTF-8/UTF-16 conversion replaces modified-UTF JNI shortcuts. Flow remains read-only.
Android lifecycle/controller extraction and a real graph evaluator are still pending.


## Current baseline — project and node architecture v1

The canonical forward design is [ADVANCED_ARCHITECTURE](ADVANCED_ARCHITECTURE.md),
with the Bengali screen/workflow guide [WORKFLOW_BN](WORKFLOW_BN.md) and ADR 0009.
Current code adds a Projects launcher, app-private catalog/storage adapter, managed
editor session paths and contextual workspace panels on the established native
edit engine. The experimental native graph module validates structure only; it is
not integrated into preview/export or document persistence. The present pipelines
remain MediaPlayer preview and Media3 render, not a completed shared graph evaluator.

The sections below preserve chronological engineering decisions and evidence. Older
"current", "pending" and remux-only statements describe those checkpoints, not the
latest selected route. Consult the canonical baseline and latest evidence entries
before implementing new graph/storage/UI work.

## Original repository snapshot (historical)

- `engine/core`: portable C++20 time primitives.
- `engine/timeline`: non-destructive track/clip value model and validated clip moves.
- `engine/commands`: move-clip command and undo/redo history.
- `engine/project`: bounded versioned binary codec (v0 migration to v1) and best-effort atomic local-path save.
- `tests/unit`: portable host unit tests.
- `app/`: Kotlin Activity with SAF video picker and project-document Save As/Open adapter, persisted media URI grant attempt, background metadata read, aspect-ratio MediaPlayer preview/playback, clip timeline/edit-history controls, background project I/O, and debounced autosave/explicit recovery. User-reported Galaxy F23 smoke test confirms visible playback and editing controls; export/sync and SAF provider validation remain incomplete. This is an early UI shell, not the processed timeline preview.
- `platform/android/jni`: synchronized, coarse-grained JNI calls for session lifetime, media-reference clip insertion, batched JSON timeline snapshots, move/relink/split/trim/delete/ripple, undo/redo, project save/load, autosave, and status summary. Project file operations are invoked on the Activity's single-thread scheduled I/O executor.
- Android external-native build packages the portable engine in `libowncut_android.so`. Preview uses separate Android MediaPlayer instances attached to a TextureView Surface, bounds each segment to that clip's source range, and auto-advances only to an exactly adjacent clip on the same track; a timeline gap stops playback. The Activity shows clips as position/width-scaled blocks in horizontally scrollable per-track rows with a shared seek bar. The current working tree adds long-press same-track drag/reorder, routed through the undoable C++ move command; the new gesture is not Android-built or device-tested yet. Cross-track drag is not implemented. This is not a compositor or shared render/export graph and may have decode handoff gaps. The Android `MediaExtractor`/`MediaMuxer` stream-copy exporter in APK `phone-test-39270709637` handles keyframe-starting clips and explicitly compacts gaps after user confirmation. Unbuilt follow-up source also permits multiple source URIs when codec configuration and track formats match, then reopens the muxed file and decodes a first frame before SAF copy. It still cannot render black gaps, re-encode mismatched formats, apply effects or mix tracks; no real-media output has been device-verified. No independent audio engine/mixer or effect renderer exists. The Galaxy F23 user previously reported black video under SurfaceView and now confirms visible video and working editing UI after the TextureView migration. This is a user-reported phone smoke test only; multi-device codec, A/V-sync and export validation remain outstanding.

## Intended boundaries

Android app shell (Kotlin + Compose/Surface integration) owns lifecycle, permissions, picker, and user interaction. A small batched JNI boundary maps immutable requests/snapshots to a C++ engine. C++ owns timeline, commands/history, scheduling, render/effect graph, DSP, project logic, and platform-independent export orchestration. Android adapters own codec, surface, storage URI, and audio-device APIs. UI never mutates engine state directly; use commands and snapshots/events. Keep decode/render/file I/O off the UI thread. Audio callbacks must be bounded and non-blocking.

## Time and edits

`Timestamp` is integer ticks plus a positive, normalized `TimeBase` (seconds per tick). Video frame rate is represented by the frame duration rational (for example 1001/30000 seconds); audio sample time uses a sample-period base. Source and timeline timestamps remain separate fields. Rescaling rounds to nearest tick, with exact half-ticks away from zero. The current implementation uses a signed 128-bit intermediate and limits timebase terms to 1,000,000,000 to keep intermediate products bounded; it is intended for GCC/Clang toolchains. Variable-frame-rate indexing, frame-boundary policy, audio clock drift, and sync scheduling remain to be designed and tested before playback work.

A clip references a media ID and source in/out range; it does not alter source media. Current track model allows overlapping clips and does not yet enforce audio/video linking or global ID persistence. Unique IDs are validated within one timeline instance. Undoable commands currently cover move, media relink, split, left/right trim, delete, and single-track ripple delete. The in-memory history is bounded to 256 commands by default (configurable per history instance); this bounds command count, not byte use for a ripple operation touching a very long track. Ripple delete shifts clips on the selected track whose starts are at or after the removed clip end; overlapping clips and other tracks stay fixed. Move and relink are surfaced through JNI; APK `phone-test-39250035336` compiled the same-track drag and selected-clip relink source. Physical-device verification remains pending. This first UI is not a scalable multitrack editor. Activity-side debounced autosave and explicit snapshot recovery use the local filesystem. The native store remains path-based; Kotlin now stages SAF project documents through temporary cache files and adds an undoable selected-clip media relink flow. Provider-side atomic replacement and write-ahead recovery journal remain absent; local Linux/Android writes now fsync data+directory. The initial selected-clip relink is included in a debug APK but not yet device-tested; the current working tree adds an atomic group update for all clips sharing that URI, pending Android build. Project schema behavior and limitations are described in `PROJECT_FORMAT.md`.

## Ownership and concurrency policy

Use RAII and explicit ownership. Future engine session should have a serialized command owner, immutable snapshots for UI reads, bounded queues for realtime handoff, cancellation tokens for stale seeks, and explicit shutdown before Android surface/audio adapter destruction. These are design constraints, not implemented behavior.

## Graphics/media feasibility

No backend is selected. Before selection, measure Android MediaCodec-to-surface interoperability and representative 1080p SDR preview on target devices; compare OpenGL ES and Vulkan support, lifecycle behavior, synchronization, memory copies, and device matrix coverage. Reuse one processing graph for preview/export where feasible. Resolve Oboe/AAudio, MediaCodec/NDK API, and any FFmpeg dependency choice only after official docs, supported API levels, maintenance, ABI, license, and packaging review. See ADRs 0001 and 0002. Android versions are pinned in Gradle files and documented in ADR 0002; toolchain/API compatibility was checked against official release documentation.

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
