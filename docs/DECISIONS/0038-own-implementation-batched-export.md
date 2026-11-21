# ADR0038 — Original implementation, batch execution and export diagnosis

2026-10-18 (session date). Accepted.

User chose our own implementation, not GPL integration, after discussing later private
repository / paid Play release. Do not copy/port Graden's shaders, algorithms or assets.
Android/Media3 remain third-party dependencies with their notices; ownership is not a claim
that every dependency is original. User asks for a chat plan, retained Markdown and larger
coherent autonomous work batches. Plan: docs/plans/OWN_IMPLEMENTATION_BATCH_PLAN_BN.md.

User now confirms live video preview works well on their phone; export fails. This supersedes
prior phone-preview pending status for their tested clip/build only. Export failure is not
measured independently. Static audit finds two shipping grade guards: makePlan and composition;
production graded export is intentionally not wired. A candidate exists. Do not remove these
guards or silently export an ungraded file to claim completion.

Batch A starts with immutable export snapshot capture, reusable shipping preflight, and full
bounded cause-chain reports for preparation/render/gallery/document-copy errors. Reports
include app version/revision, clip/active-grade count and720p preset; explicit local Copy.
Preparation failure happens BEFORE worker startup and needs the same report. Cancellation
keeps its distinct route; cleanup behavior and content/timing/gap policy remain unchanged.
Exception messages may contain provider/platform paths; no telemetry, automatic upload or
extra URI/filename collection. Dialog is selectable and refuses destroyed/finishing activity.

Local actual pure Kotlin tests cover all phases, context bounds and nested/long/cyclic causes.
Native6, Python27 and resources345/3themes/17labels pass. Android compile/phone execution
pending at implementation commit. No new native/JNI algorithm or dependency in this change.
No graded-export enablement, codec/GPU/10bit/parity claim. Readiness25%/75%.

Candidate audit: pinned Media3 DefaultCodec.convertToFormat delegates to MediaFormatUtil,
which reads KEY_WIDTH/HEIGHT and does not retain explicit crop. Added Android fixture
regression for this known transport limitation; not measured phone geometry. Candidate
guard errors now distinguish input/output, expected/actual and unavailable raw crop;
no unsafe inference or guard relaxation. Official pinned sources:
https://github.com/androidx/media/blob/1.8.1/libraries/transformer/src/main/java/androidx/media3/transformer/DefaultCodec.java
https://github.com/androidx/media/blob/1.8.1/libraries/common/src/main/java/androidx/media3/common/util/MediaFormatUtil.java

### ADR0038 first-batch CI evidence

Source a82d563a669a987807ff9b59e1bc633d9a7f28a7: Android42772350310 and Portable42772350391
PASS. Assembly/lint/device-test source compilation/alignment verified; instrumentation,
emulator and phone export NOT executed. Local native6/pure Kotlin reports/Python27 and
345 bilingual resources PASS. No public fresh-install APK for this diagnostics checkpoint.
Graded shipping export remains blocked. See docs/plans/OWN_IMPLEMENTATION_BATCH_PLAN_BN.md
for own-code execution plan and remaining adapter/output acceptance.25%/75% unchanged.
