# ADR0042 — Saved-grade main viewer, exclusive viewer layers and reachable export entry

Session date2026-10-18. User-phone report: color editor live grade visible; after Apply main
viewer is ungraded; screenshot shows duplicated/cut image and known-color interpretation
warning; export only shows refusal toast. No new APK unless user explicitly requests one.

Confirmed code gaps: Apply persisted native grade but main viewer used only MediaPlayer;
launchExportPicker called makePlan before startTimelineExport, so active-grade refusal
prevented reaching the experimental confirmation in Details. Remove ONLY duplicate launcher
validation; normal plan/composition guards stay, explicit candidate consent remains required.
No automatic candidate/ungraded fallback or quality claims. Error text now describes route.

MainGradePreview is a separate Android viewer adapter for saved selected clips: strict source
probe off-main, bounded cancellation/close, guarded InlineGradePreview and black overlay. Main
MediaPlayer is paused beneath it; never show original as successful graded video. Route Play,
pause, source seek, position tick and completion through saved-grade player. Clip completion
reuses existing timeline advance; no timeline model/view/edit semantics changes. Release on
selection/player lifecycle; stale probe/draw callbacks closed. Close main-grade decoder before
opening Color editor; restore saved grade on dialog close after editor resources release.
Undo/redo/reset/selection refresh compares immutable current clip; removing grade restores
underlying player at current source position. Shared MainActivity wiring coordinator-owned.

InlineGradePreview gains explicit source seek/play-state and nonlooping end callback; Color
editor default loop behavior unchanged. UI acknowledgment is shader draw/first-frame policy,
not measured phone display or decoded-pixel parity. Source interpretation remains preview-only
and is not transferred into saved main viewer/export. Failed grade source stays explicit.

Color viewer removes reference ImageView AND drawable on live handoff and restores retained
reference only on failure. Texture is nonopaque, viewport black/clipped. This addresses a
layer-composition risk; screenshot's exact GPU/decoder root cause not independently measured.

Input UI distinguishes known tags (information only), missing interpretable SDR tags (explicit
opt-in), and missing tags with HDR/unsupported evidence (blocked). Existing resolve/decoded
semantics unchanged; known output can still validate during an explicit missing-tag preview.
Localized copy no longer says main viewer is always ungraded after Apply.

Tests: new actual pure Kotlin policy regression; source-wiring tests explicitly static, not
Android behavior proof. Device needs Apply/main play/seek/cut/reset/history, repeated open/close,
viewer letterboxing, export consent/failure/output and lifecycle validation. No APK requested,
so no fresh-install marker/publication. No Graden code/dependency/schema/JNI changes.25%/75%.
