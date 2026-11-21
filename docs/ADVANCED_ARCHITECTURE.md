# Owncut advanced architecture — design baseline v1

## Priority update — 2026-10-23

User direction/ADR 0016 now makes **color grading the first advanced implementation
phase**, with C++20 domain/processing logic. The explicit current phase sequence and
exit gates are in IMPLEMENTATION_PHASES_BN. The original architecture/research below
remains the forward design, not a requirement to finish controller extraction before
starting color. Native grading reference/frame bridge is implemented and host-tested;
project/history, live preview/export, GPU and editable node integration remain pending.

Status: **accepted direction, incrementally executable**, 2026-10-22. This is an
editable baseline, not a claim that the full engine already exists. Change it with
an ADR, compatibility impact and tests, not by silently changing saved output.

## 1. Research and original-product boundary

Public official material inspected on 2026-10-22 (page text, not installed products,
private source, full training videos or an exhaustive benchmark):

- [Blackmagic official training](https://www.blackmagicdesign.com/products/davinciresolve/training):
  separates editing, color, effects and audio learning/workflows.
- [Official Fusion workflow](https://www.blackmagicdesign.com/products/davinciresolve/fusion):
  explicitly describes a node tree, media input, effects, merge, mask, inspector,
  spline/keyframe editing and moving between edit/effects/color contexts.
- [Official collaboration/project-library overview](https://www.blackmagicdesign.com/products/davinciresolve/collaboration):
  describes project libraries and proxy/original association. Cloud collaboration
  is not a requirement for Owncut's first implementation.
- [Node Video official Quick Start](https://nodevideo.com/guide/quick-start):
  layer/root selection, timeline, contextual properties, keyframes/curves, preview
  quality independent of export, and projects/drafts on returning home.
- [Node Video official FAQ](https://www.nodevideo.com/guide/faq): preview cache
  invalidation, resource pressure and preview/export quality separation inform the
  need for bounded caches/capability diagnostics. Vendor sync/quality claims are not
  independent performance evidence for Owncut or a promised result.
- [Node Video project exchange](https://www.nodevideo.com/guide/import-export-project):
  distinguishes editable project exchange from rendered video; documents packages
  and limitations. Owncut does **not** implement their package or template format.

Synthesis: use a local project shelf, timeline for editorial time, typed processing
connections for image/audio transformation, contextual inspector, shared selection,
explicit delivery. Layer ordering is not itself a general node DAG. Do not conflate
Node Video's documented layer/root interface with Fusion's exact graph design or
infer either product's internal engine architecture. All module contracts, names,
layouts and code below are Owncut decisions; no copied assets, wire colors, UI or
proprietary algorithms. No new third-party dependency is selected here.

## 2. Product information architecture

```text
Launch → Projects shelf
           ├─ New (name) → new independent native project → Editor
           ├─ Import .owncut (bounded + native validation) → new local copy → Editor
           ├─ Existing card → load saved primary → Editor
           ├─ Rename (catalog label, not file-path or media change)
           └─ Recovery badge → open primary → explicit Recover in editor

Editor: header [project name / Projects] + workspace selector
  Persistent viewer / playback / timeline / selection / Undo / save / status
  Edit     editorial tools and import
  Media    current source information, import/relink (future: bins and offline media)
  Flow     CURRENT: read-only explanation of separate preview/export routes
           FUTURE: editable clip/composition graph and contextual node inspector
  Deliver  output capability, render/cancel, validated publication
  FUTURE: Color, Audio, motion/curve panels only when their engines actually work

Projects / Android Back → checkpoint dirty managed project → success → shelf
                           failure → stay in editor, show error, preserve recovery
```

Portrait: one active contextual tools panel; viewer, transport and timeline are not
recreated when selecting a page. Landscape/tablet target: resizable viewer/timeline,
media drawer, inspector, graph canvas. Do not copy a desktop application's seven-page
layout into a phone or expose fake controls. Flow currently describes the **existing**
route; it is not editable and does not claim graph-rendered pixels.

Empty, loading, offline, recovering, rendering, cancelling, saving and error are
first-class states. Loading failure must not expose a blank writable replacement.
Project I/O/export gates mutations; cancellation is explicit. Recovering a draft and
opening a document replace state only by consent. Existing gap compaction still
requires consent. Selecting a workspace must not alter output, seek or recreate the
TextureView. No start-up camera/storage permission request: ask through SAF as needed.

## 3. Object identities and data boundaries

| Object | Authoritative owner | Stable relationship / persistence target |
|---|---|---|
| LibraryProject | Android catalog | UUID, label, last opened/checkpoint date; owns local document path |
| Project | C++ project domain | project ID, schema version, project settings, sequences, assets, graphs |
| Asset | future C++ media model + Android URI adapter | asset ID → original locator, fingerprint, stream metadata; proxy is replaceable |
| Sequence | C++ timeline | rational rate, ordered tracks, clips, markers, output graph binding |
| Clip | C++ timeline | media reference, source in/out, sequence start; future graph ID and retime map |
| Graph / Node / Port | C++ graph | graph/node stable IDs, typed ports, registry type/version, parameters |
| Animation | future C++ automation | parameter path + rational time domain + interpolation + keyframes |
| UiSession | Android | selected project/clip/node, page, zoom/scroll, playhead; not render authority |
| RenderJob | future orchestration + platform encoder | immutable project revision/plan/settings; progress/cancellation/result |

Current file schema stays v1: existing binary timeline remains authoritative. The
catalog is an app-private **sidecar**, not a second editable timeline. Its friendly
label is not yet an embedded native project rename; exporting and reimporting asks
for a label. Catalog UUID also is not the native document's internal project ID;
a future duplication command must issue new domain IDs where necessary.

Target v2 sections: settings, asset registry, sequences, graphs, automation and
feature requirements. Each section bounded and versioned; reader validates references
and resource budgets before replacing session. Unsupported required nodes reject
editing/export rather than silently bypassing effects. Unknown optional metadata may
round-trip; unknown required rendering sections may not. Older v1 documents migrate
by constructing identity clip graphs and a sequence output, with golden fixture tests.
**No v2 writer/migration ships in this batch.** Never write transient compiled plans,
GPU handles, surface references or caches into a portable project document.

## 4. Node architecture and semantics

Timeline answers *when/which source range*; the graph answers *how samples become
output*. The timeline is not replaced by arbitrary wires. Proposed hierarchy:

```text
Asset reader → source-time map → clip-local image graph → sequence compositing graph
            ↘ clip audio graph → bus routing / mixing → sequence audio output
sequence image → color/output transform → render request
```

Each clip graph is evaluated in clip-local source/sequence time via an explicit
`EvaluationContext`, never a UI slider's milliseconds. Composition graphs may contain
nested sequence references; validation must include cross-graph recursion, not only
cycles within one graph. Default identity graph produces unchanged source output.
Nodes are nondestructive data, with commands for add/remove/connect/disconnect,
parameter changes and atomic multi-step edits. Layout coordinates are UI metadata.

### Structural contract implemented now

`engine/graph` is a portable **experimental validator only**: stable nonzero integer
node IDs; image/mask/audio/scalar/geometry ports; bounded nodes (4096), edges (32768),
ports (32 per direction), type-name bytes (128); valid references/indexes and matching
port types; one writer per required input, fan-out allowed; deterministic topological
order (smallest ready ID first), no cycles, and every node contributes to the single
sink. Iterative traversal avoids recursive-stack exhaustion. No node registry, graph
serialization, JNI, UI editing, cache, GPU work or actual effects evaluation yet.

The initial contract requires all inputs; parameters/defaulted/optional sockets and
multiple output domains will extend it with explicit versioning before persistence.
Node type strings alone are not capabilities: a future registry must whitelist type
and version, parameter schemas/ranges, backend requirements and deterministic behavior.

### Target evaluator contract (not yet implemented)

`GraphCompiler(snapshot, output, backendCaps) → ValidatedPlan | Diagnostic[]`.
`Evaluator(plan, context, cancellation) → ImageHandle | AudioBlock | Error`.
Context includes immutable project revision, sequence/source rational times, requested
output rectangle/resolution, quality policy, working color space, stream index and
seek generation. Image metadata includes pixel format, dimensions, color primaries,
transfer, range, premultiplied-alpha convention and GPU ownership/fence. Audio uses
sample-index time, sample rate, channel layout and bounded block sizes; never route
it through video-frame tick rounding.

First executable nodes: Source, TimeMap, Transform2D, Opacity, Merge, Mask, ColorAdjust,
ImageOutput; audio Source/Gain/Mix/Output independently. Typed ports prohibit implicit
audio/image conversion. Masks define coverage and coordinates. Color operations
require explicit linear/display transforms; SDR and HDR must not share an undocumented
assumption. Expressions/plugins, AI, 3D, optical flow and custom shaders are later,
sandboxed/capability-gated work, not arbitrary executable project strings.

## 5. Modules and migration seams

```text
Android shell (Projects, Editor, navigation, accessibility)
  → application/session coordinator (commands, revision, selection, job state)
    → narrow batched JNI facade
      → C++ project + asset references + timeline + history + graph compiler
        → render scheduling / automation / audio processing
          ↔ Android storage, metadata, decoder/encoder, surface, audio-device adapters
```

Existing modules: core/time, timeline, commands, project, graph-contract; Kotlin
ProjectLibrary/Activity, MainActivity, EngineSession, TimelineView/Geometry,
ProjectDocumentIo, MediaPlayer preview, Media3 render, ExportPublication.
`MainActivity` is still too large: current UI grouping is a migration seam, **not** a
completed clean-architecture rewrite. Next extraction: EditorCoordinator with a
serialized command owner, immutable UiSnapshot and explicit state machine; then
PreviewController, ProjectRepository and RenderJobController. Do not move engine
math into Kotlin during extraction. Catalog filesystem/SAF metadata stays Android;
edit semantics, validation and timing stay C++20.

## 6. Scheduling, playback and concurrency

Target: serialized command queue publishes immutable revisioned snapshots. Compiler
and media I/O operate on workers; UI reads cached state only. Decode/render scheduler
uses bounded queues, backpressure and cancellation generations. Last requested seek
wins without allowing an older decoder to publish its frame. Separate video, audio,
I/O and export scheduling priorities; no blocking locks, allocation or file I/O in a
real-time audio callback. Resource handles carry ownership and lifecycle generation.

Current preview remains MediaPlayer sampled every 50 ms and SEEK_CLOSEST completion;
current seeks are serialized/rejected while busy, not a completed last-seek-wins
scheduler. Rotation retains the engine, source position, playhead and page; process
death does not retain undo and requires saved-document load / explicit recovery.
Current render uses a separate Media3 plan. No exact shared preview/export pixel
parity or sample-accurate A/V-sync claim until one evaluator is integrated and tested.

Preview can use proxies, adaptive render scale and cached intermediate frames in the
target design. Final export uses originals/full requested quality. Cache identity:
asset fingerprint + graph revision/subtree + time + resolution + color + quality +
backend version. Memory budgets include decoded images, textures, audio and in-flight
jobs; LRU eviction and thermal/background pressure must be measured on devices.
Reclaimable proxies/cache must never be the only saved original. Renderer selection
(OpenGL ES vs Vulkan), audio backend and RAW SDK remain undecided pending spikes,
maintenance/license review and device measurements, not speculative dependency adds.

## 7. Persistence, recovery and navigation implemented now

App-private `files/projects/<UUID>/metadata.json` (Android AtomicFile, schema 1, bounded
16 KiB) plus `project.owncut` (native atomic save), `project.owncut.autosave`.
Catalog operations serialize process-wide across Activity recreation. Creation rejects
a full library before writing a new project. Listing validates at most 1000 entries, sorts by last-open/checkpoint metadata, and reports
bad metadata without deleting documents. Metadata and native document are not one
transaction: a crash can leave an orphan directory; report and preserve it, do not
silently delete or fabricate a catalog card. Project names 1–80 characters, no control
characters; ID validation disallows path traversal. Media stays as URI references.

New project writes a real empty native project. Import makes a bounded cache staging
copy, validates via native load, writes a new managed document and leaves original
external file unchanged. Returning to Projects saves dirty primary then clears its
recovery snapshot; if no edits were made, an existing unchosen recovery remains.
Failed checkpoints stay in editor. Unexpected shutdown uses existing debounced
recovery; this is not a durable command journal. Recovery is explicit and its result
is marked dirty for managed Save/Projects checkpoint. Save As also checkpoints the
managed copy before clearing recovery. A failed external provider replacement is not
transactionally rolled back; do not claim SAF atomicity.

Legacy `active.owncut` and recovery are copied once to a known library entry without
deleting or rewriting original files. Corrupt legacy input reports an error; it is
not replaced with an empty project. A partial copy/crash still requires recovery
hardening; this is not a filesystem-wide transactional migration.

Local library is not a backup: uninstall/data clear loses app-private files and URI
grants. Save external project documents and retain original media; relink when grants
expire. Current rename changes only catalog label. No delete/duplicate/cloud function
is exposed before backup, confirmation and identity policies are implemented.

## 8. Delivery and safety invariants

Target export consumes an immutable validated graph/timeline revision, uses the same
processing semantics as preview at final quality, preserves timeline gaps as black/
silence unless a separate explicit operation changes editorial time, checks storage,
validates output, then publishes atomically where provider APIs allow. Capability
checks precede allocation: node backend support, formats, resolution, HDR, audio and
resource budget. Cancellation completes worker ownership before resource reuse.

**Current** export remains source-cadence H.264/AAC, 720-high SDR, one sequential video
track, explicit gap compaction consent, overlap rejection, silent fill where needed,
validated MP4 and Q+ Gallery publication; Android 8–9 SAF output. UI architecture
changes must preserve this behavior. It is not a general multitrack graph exporter.
`.ccraw` awaits a specification, sample files, color/calibration/compression details
and decode licensing; reserve an asset-reader seam, do not guess the format.

## 9. Incremental execution and acceptance gates

| Phase | Deliverable | Gate |
|---|---|---|
| A, this batch | local shelf/navigation, isolated projects, existing tools grouped, read-only route, experimental native DAG validator | host + Android build/lint + full regression + new catalog/workflow tests; then phone UX |
| B | coordinator extraction, bounded snapshots, durable recovery transactions, catalog repair, media bin | old/new document fixtures, fault injection, rapid lifecycle/navigation and offline relink |
| C | persisted identity graphs + registry + source/transform/output evaluator spike | v1→v2 golden migration, cycle/type/budget tests, GPU lifecycle/device baselines |
| D | shared preview/export graph, merge/masks, black gaps, multitrack + audio clock | pixel/PTS/duration equivalence, channel/sample tests, cancellation/pressure/device matrix |
| E | keyframes/curves, color management/scopes, audio automation, titles | deterministic animation/golden images/audio and HDR/SDR reference measurements |
| F | proxy/cache, advanced tools, custom RAW, performance/production packaging | profiled budgets, format conformance, codec/ABI/licenses/security, stable signing and recovery |

Not implementing phases C–F by adding buttons. No fps/4K/HDR/RAW slider without engine
support. Each phase can be revised through an ADR; do not infer deadlines from this
ordering. Source-cadence export and 30-fps editorial grid currently differ deliberately;
configurable project rate must update native serialization, UI geometry, JNI and tests
as one change, not just expose a preference.

Normal navigation has one editor. Before adding multiple windows/sessions, implement
per-project writer leases and revision/conflict checking across sessions, not only a
command queue inside one session. Concurrent independently opened writers are not a
supported conflict-resolving workflow in this batch.

## 10. Evidence and remaining validation

Baseline playhead batch `8f5df51`, Android run `39728248796`, portable `39728248775`
passed; user now reports the playhead APK works well on their own phone. This report
is distinct from independent performance/sync measurement.

This architecture batch's execution evidence is recorded in PROGRESS and BUILD_AND_TEST.
New tests cover structural DAG acceptance/rejection, isolated catalog persistence,
non-destructive legacy migration, malformed metadata/path protection, launcher route,
managed checkpoint/reopen and page retention. Physical phone checks still needed:
edge-to-edge layout, small screen/large fonts, Back/save failure, multiple projects,
process kill/recovery, URI providers, and continued rendering/Gallery behavior.


Final execution: `f652c38`, Android run `39748116540` (job `119721585167`) and
portable run `39748116580` passed. APK/checksum prerelease `phone-test-39748116540`
was verified as uploaded, targeting the exact tested source. Details, the initially
incorrect test-fixture path and optional report-storage quota limitation are recorded
in PROGRESS/BUILD_AND_TEST; no claim of physical-device validation for this batch.

### Phase B implementation checkpoint — 2026-10-22

Managed project I/O now goes through a retained single-writer store: bounded stable
read snapshots, fingerprint preflight before native atomic checkpoint, validated
recovery and explicit catalog repair preserving metadata. MainActivity still owns
editing/lifecycle orchestration; **the full EditorCoordinator and durable revision
journal are not done**. No evaluator/compositor is implied by this storage work.

Readiness v1 weights/evidence live in `plans/readiness.json`; run
`python3 scripts/progress.py`. Credits are estimated scope weights, not hours or
professional feature equivalence. Signing delivery is a separate blocked package;
UPDATE_APKS specifies secure owner setup and the lost legacy-debug-key limitation.


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
