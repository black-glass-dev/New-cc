# ADR0025: Unsaved color draft playback

Date: 2026-10-24. Status: implemented source, Android validation pending.

## Decision

Connect a Color-editor action to experimental LiveColorPreview using an immutable copy
of current draft values. Parse Bengali/English input and call existing native shader
validation before opening. No new grading math, dependency or history command. Activity
checks session identity, busy/destruction state and exact saved clip equality first.
Source-time position is captured at editor opening; draft playback keeps clip trims.
The isolated shared pipeline consumes grade settings, not the saved gradeActive predicate.

Editor dismisses its child player on closing; MainActivity also tracks that same dialog
for existing onStop cleanup. Returning from playback leaves fields unchanged. Playing
a draft does not mark it accepted or save it. Apply still requires successful matching
still render and exact original clip/session revalidation at commit. Dirty fields disable
Apply and still comparison, explicitly label the old image, and stale asynchronous render
completion retains dirty feedback unless captured settings equal current settings.

Saved and unsaved playback use the same generic snapshot notice. No paused replay API,
shipping graded-export guard removal or live/export parity claim.

## Evidence / limits

Pinned Kotlin2.3.21 -Werror with actual JNI passes bilingual parsing, immutable preview
capture, enabled/bypass and native invalid/NaN rejection plus existing regressions.
301strings/3themes/17labels pass. These do not compile or execute Android UI code.
Latest Android run40084821664 has zero steps and account billing/spending-limit failure.
No new Android build, GPU/codec/device evidence, signing execution or downloadable APK.
Phone testing remains user-owned after a real update-compatible build is available.
Readiness unchanged25% complete/75% remaining.
