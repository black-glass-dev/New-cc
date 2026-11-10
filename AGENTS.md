# Engineering guidance

- Preserve the original-product identity; do not copy proprietary code, branding, assets, algorithms, or exact UI from other editors.
- Keep portable editing/domain logic in C++20. Android lifecycle, permissions, storage, surfaces, and codecs belong in small Kotlin/platform adapters when introduced.
- Keep timeline time integer/rational and distinguish source, timeline, frame, and audio-sample domains. Document conversion and rounding.
- Validate all project/media-derived input. Keep edits non-destructive and IDs stable.
- Do not put I/O, blocking locks, or unbounded allocation in a future real-time audio callback.
- Add tests with each behavior change. Never claim an Android/device feature is verified by host-only tests.
- Update `docs/PROGRESS.md` with only tests actually run and current limitations.
- Avoid adding dependencies unless maintenance, license, Android/ABI support, security, binary size, and relevant page-size compatibility are checked and recorded.

- Treat `docs/ADVANCED_ARCHITECTURE.md` and ADR 0009 as the editable forward baseline. Keep implemented vs planned capabilities explicit; update the baseline through ADRs for incompatible changes.
- Projects shelf metadata belongs to the Android storage adapter, never duplicate timeline/graph edit semantics there. Never silently overwrite a failed project load or discard an unchosen recovery snapshot.
- Workspace navigation must preserve the live viewer/timeline; current Flow is read-only, not an effects renderer. The experimental C++ graph validator is not yet a persisted/evaluated graph.

- UI text must have default English and Bengali resources with matching format arguments. Run `python3 tests/check_ui_resources.py`. Keep project/user content and native identifiers unchanged by language selection.
- Editing action buttons use original semantic icons, localized accessibility/tooltips and at least 48dp targets. Keep confirmation/language-choice copy readable; don't replace consent with ambiguous icons.

- Follow ADR 0011: keep default Edit quiet and prioritize viewer/transport/timeline. Put metadata, source-only seek and secondary tools in contextual panels; important errors/recovery must still notify users. Palette roles live in `studio_themes.json`; test every theme, not just default colors.

- Publish downloadable APKs only with durable signing/pinned public certificate and monotonically managed versionCode; never call random debug artifacts install-over patches. Never store private signing material in Git, chat or release assets.
- Keep readiness.json weights/status/evidence explicit; only verified packages earn credit. Preserve writer ownership across rotation and do not bypass guarded checkpoints for managed projects.

- Native project/history mutation belongs in SessionCoordinator, not JNI fields. Keep JNI result keys and Kotlin synchronized-handle lifetime contract compatible.
- Pages reject stale revisions; do not silently assemble mixed snapshots. Whole JSON/Kotlin lists are still materialized; do not describe this as end-to-end bounded UI memory.
- save_copy must not mark a primary checkpoint clean, and restore_draft must stay dirty. Native saved revision does not replace Activity dirty state until managed checkpoint/catalog transactions are unified.

- ADR 0014: phone testing is user-owned. Default CI is native/build/lint/static/test-source checks; emulator/render/install-over are manual opt-in. Always disclose skipped tests. Signed candidates must still match the approved APK signer/package and monotonic version.
- Checkpoint receipts are diagnostic, not a WAL or authority to discard recovery. Archive only typed malformed receipts with a readable primary; preserve future formats/I/O failures. Keep strict UTF-8↔UTF-16 JNI conversion and UUID project identity.

- ADR 0015: JNI tokens must never be pointer casts/reused IDs; pin a registry owner through each call. Snapshot cache publication requires a complete checked revision, not partial pages. Viewport culling must preserve overlaps, painter order and dragged clips. Run optional production Kotlin host tests when changing these projections; they do not validate Android lifecycle/rendering.

- ADR 0016 / IMPLEMENTATION_PHASES_BN: user reprioritized COLOR GRADING FIRST. Continue phase 1B clip/history persistence and then shared color-managed preview/export; do not return to infrastructure-only batches without feature rationale. C++ owns grade math/validation/scopes; Kotlin is narrow UI/platform transport. Preserve explicit sRGB/straight-alpha input contract, identity/source/alpha and future-version safety. Do not call channel balance Kelvin or the standalone preset project persistence. No fake Color controls before actual processing integration.

- ADR 0017: Clip.grade is native per-ID data. Use SetClipGradesCommand for atomic history; retain parameters on bypass, inherit on split, preserve on other edits. Project writes v2, reads v0/v1/v2; future inner grade format changes require an outer schema bump. Never silently downgrade/strip grades. Keep unrendered active grades blocked at both render/remux boundaries until phase 1C parity passes. Native gradeActive metadata must match the immutable grade; do not fabricate/mutate that relationship in Kotlin plans.

- ADR 0018: color_gpu owns explicit texture profiles/native GLSL. Media3 1.8.1 DEFAULT is encoded SMPTE170M, not GlEffect's stated linear SDR. Candidate effect MUST stay paired with its explicit factory; require known BT709 SDR metadata and full float32 fragment highp, reject unsupported GPU budgets/HDR rather than drop grades. Candidate is NOT live preview/export; keep guards until actual integration/decoded parity. Software EGL results are not Android/device/codec proof.

- ADR0019: raw source tags are not Media3 fallback ColorInfo. Native policy admits only Rec709/SDR/known range/no HDR-static data. Keep probe local/worker-owned/finally-release, request-local dedup, immutable all-or-nothing plan. Candidate composition is NOT live export and must use its explicit factory; preserve gap consent and shipping guards. No URI byte pinning/freshness guarantee; verify actual decoder format before enabling preview/export.

- ADR0020: candidate decoded gate verifies every rendered release (both Codec overloads), not a first-frame cache. Unknown/changed output/query failures are terminal; keep timestamps/audio/drop cleanup, owner-thread and idempotent release. Candidate.configure(context,builder) must bind guarded asset loader + GL factory. Format descriptor matching is NOT URI byte identity. Host gate tests do not validate Android codec/thread/pixel behavior; guards stay until real shared-preview/export acceptance.

- ADR0021: Color still editor must say STILL/ungraded playback/guarded export. No project mutation on draft/render/Cancel; only successfully rendered unchanged draft can Apply once, with captured clip/session guard and dirty/autosave. Keep bitmap sRGB/straight-ARGB transport explicit and stale dialog results rejected. Candidate encoder entry is real but test-only/unverified; do not remove shipping guards or claim video parity from a scaled CPU proxy.

- ADR0022: isolated live Color preview is opt-in/saved-snapshot/experimental, not timeline replacement or shipping export parity. Specialize default MediaCodecVideoRenderer's protected graph hook, use explicit native-grade DEFAULT factory and raw-format gate. Keep sink ownership/release with base renderer/ExoPlayer, cancel stale startup on close/background. Before only during playback (no private replay-cache reflection). Android custom-renderer support/thread/pixel behavior is unverified; do not credit host tests as codec proof.

- ADR0023: compare_rgba8 is byte-domain diagnostics, not color conversion/calibration. Keep readonly/alias/stride/padding safety and native metrics math. Gray fixture/candidate encode tests are Android-unrun; proposed codec thresholds are NOT results. Never retag unknown imported media or skip/fall back to make parity green. Gray ROI/native oracle doesn't establish live-display/all-color/temporal parity; guards stay.

- Preview failure/close are terminal: clear ownership before release; reject late callbacks.
  Keep touch/keyboard/accessibility seek on the same exclusive-end transport policy. CPU
  proxy bounds are not decoder memory or export limits; never restore full-resolution
  fallback on API26 oversized sources. See ADR0024; Android behavior remains unverified.

- Draft live preview must not write session/history or authorize Apply. Validate native GPU
  budget, capture immutable values and reject stale session/clip. Parent dismissal closes
  the child player; activity backgrounding releases it. Dirty still feedback must not
  imply the current draft rendered. See ADR0025.

- Self-hosted APK builds must be explicit manual dispatch on the tracked branch and use
  dedicated trusted labels; never route PR/push to signing machines. Original key/pin/
  approved-baseline preflight does not replace final APK verification. Billing and lost
  original keys cannot be fixed by application code. See ADR0026 and recovery guide.

- Shared grade shaders must explicitly declare highp sampler2D as well as highp floats.
  Mesa default lowp sampling caused a measured parity failure; do not relax gates to hide
  it. Android compilation now passes, but device/encoder/parity remains unrun (ADR0027).

- ADR0028 explicitly permits the current user-authorized fresh-install debug candidate.
  Only publish through separate commit-marker opt-in route; never call it update-compatible.
  Warn of uninstall data loss, do not clear a phone, and preserve stable patch signer gates.

- Fresh-install candidate fresh-install-40096664665 is actually published (source f9f7464),
  not phone-accepted. Keep its source/digest and uninstall data-loss warning distinct from
  stable update compatibility. Documentation-only follow-ups do not rebuild that artifact.

- Final workspace assembly must reattach every contextual panel after root.removeAllViews.
  Color actions need visible labels, not icon-only StudioButton; test final attached/shown
  hierarchy across tabs/themes (ADR0029). Compile-only is not phone acceptance.

- ADR0030 supersedes render-before-Apply: native-validated settings may be saved without
  available decoder, but cannot authorize unsafe rendering or claim graded timeline/export.
  Slider ranges are transport only; preserve saved extremes and exact reset identity.
  Bound/debounce preview, discard stale results, expose nested live errors. Null input-only
  output-format callback is not permission to skip raw validation of rendered buffers.

- Slider candidate fresh-install-40101757559 is actually built/published at1f1bd21;
  pure controls/diagnostic tests pass. Interaction regression is compile-only. Do not
  infer user's live failure root cause or universal source/GPU support from this build.

- ADR0031 keeps viewer/footer outside controls scroller. Timeline/decoded platform reference
  is explicitly UNGRADED, never native render input before source/bitmap admission. Preserve
  metadata errors across adjustment; no empty black rectangle or giant diagnostics viewport.
  Custom scrubber retains SeekBar accessibility/keyboard semantics; fine step is transport.

- Precision workspace fresh-install-40165432408 is published at57879db; regressions compile
  but visual/touch/reference acceptance remains user-owned. Do not label reference-only
  display as native graded success or promise decoded frames when retrieval fails.

- ADR0032: no slider UI in Color; C++ owns wheel mapping. Do not call UI discs proprietary
  grading algorithms. HDR presence ignores only absent/exact25-byte all-zero padding;
  malformed/nonzero still blocks. Never infer HDR from bit depth or claim10-bit output
  from8-bit proxy tests. User's exact HDR payload is still unmeasured.

- Current wheel candidate fresh-install-40199335976 atde4bad3 is built/published. Native/
  actual JNI policy tests pass; UI tests compile only. Do not infer user's raw HDR payload
  or10-bit output acceptance. Keep live error details separate from bounded player status.


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

## Current original-code batch contract (ADR0038)

Follow docs/plans/OWN_IMPLEMENTATION_BATCH_PLAN_BN.md. No Graden code/shader/assets ports
or GPL integration. Preserve third-party notices. User confirms live preview success;
export unverified/failing. Keep shipping grade guards until real candidate acceptance;
makePlan also rejects active grade before worker, so report preparation failures too.
Own coherent batches; do not ask routine implementation questions. No fake frame/10bit/
export success, grading sliders, silent content changes or destructive migration.
