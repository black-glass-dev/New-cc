# ADR0021 — real clip Color controls and native still preview

2026-10-23. Implemented in source; Android app compilation/execution pending.

## User-visible processing, not another native-only checkpoint
Add original Color workspace/icon, contextual clip editor with17 labeled v1 parameters,
enable/bypass, draft reset, explicit Render draft, Before/After and Apply/Cancel. Labels
and messages exist in exactly English/Bengali; parser accepts Latin/Bengali decimal digits
and Unicode minus, leaving grade validation/math to C++. Current playback is explicitly
ungraded and normal graded export remains guarded. This is NOT completion of phase1C/1D.

Worker probes explicit local-source metadata, rejects unsupported HDR/unknown tags, then
retrieves the selected source-time frame. Require sRGB ARGB8888 bitmap, detach straight
ARGB ints, scale preview proxy to <=1280x720, pack native RGBA8, grade in C++ and unpack
into a real displayed bitmap. Native GPU recipe validation also rejects unsupported grade
budgets before rendering/Apply. This is an actual image-processing route, not fake controls
or an overlay pretending to be live playback. Scaling occurs before CPU grading; candidate
video scales after grading. Therefore this proxy is NOT an encoded-output parity oracle.
Native linear grade math is shared; source conversion/scale order/codec proof remains open.

Drafts do not mutate session/history. Every input change invalidates Apply. Only a successful
render of exactly the current draft authorizes Apply. Cancel/close preserves the saved grade;
Apply checks session ownership, project I/O and exact captured clip equality, then issues
one native atomic set-grade command and marks Activity dirty/schedules existing autosave.
Same grade is a no-op. Undo/redo and v2 recovery reuse existing native history/persistence.
Dialog closes on Activity destruction. One worker owns extraction/CPU processing; stale
results cannot update a dismissed view. Bitmap recycling never races RenderThread/native
consumers. Provider/frame extraction can still block despite cooperative cancellation;
full-frame retrieval allocation and actual phone responsiveness remain unmeasured.

## Actual candidate encoder entry (not shipping enablement)
exportGradedCandidate now probes/captures the candidate and invokes the REAL existing
Transformer encoder/cancellation/output-verification/cleanup runner with guarded asset
loader + shared GPU factory. It is an explicit developer/test API, not a user export button
or successful video result. Existing ungraded export calls the same runner with no candidate;
shipping active-grade guards remain. Native/host tests cannot prove this Android refactor.

## Evidence
Passed locally: CMake6/6 native regressions; production Kotlin2.3.21 -Werror / real JNI
ARGB->RGBA->ARGB grade/bypass/channel/alpha/source checks (128gray->176), malformed shape,
Bengali parameter parsing; existing decoder/plan/projection/pixel tests;294 strings/all3themes,
17 labels per language,7metadata and3install-over tests. No new native math/dependency.

Added uncompiled/unrun Android ColorStillPreviewTest for real Bitmap transport and draft/
apply history; candidate pre-cancel/guard/no-worker-leak test source. Full Android build,
UI/dialog/rotation/provider lifecycle, actual still extraction, GPU/codec candidate execution,
encoded comparison, live graded playback, phone/install-over remain UNVERIFIED. No APK.
CI billing and absent local SDK remain release blockers, not excuses to stop development.
Verified roadmap stays25%/75%; no claim color grading is complete or APK is delivered.
