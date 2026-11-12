# ADR0034 — Source color evidence and explicit preview interpretation

Date: 2026-11-03. Android candidate published; phone acceptance pending.

## New evidence, not an inferred codec diagnosis
User reports168a106 Play draft fails: `grade source requires explicit Rec709 primaries`.
Previous de4bad3 draft success is historical user evidence, NOT current success. The new
message proves admission failure, not whether source primaries are missing or genuinely
non-709. Source bytes/HDR payload remain unavailable. No end-to-end10-bit claim.

## Research / original design
- Blackmagic official Color page: primary wheels balance overlapping tonal ranges; they
  are not pixel paint/fill. https://www.blackmagicdesign.com/products/davinciresolve/color
- Reviewed a Resolve19 interface photo (Fstoppers Micro Color Panel review): quiet dark
  interior, chromatic rim, compact shared controls. No image/assets/code are integrated.
  https://fstoppers.com/reviews/trying-out-davinci-resolve-micro-color-panel-davinci-resolve-19-679107
- Android official ColorInfo documents common assumed SDR defaults; those are NOT proof of
  source tags. https://developer.android.com/reference/kotlin/androidx/media3/common/ColorInfo
- Pinned official Media3 1.8.1 MetadataRetriever.Builder/retrieveTrackGroups/close inspected
  in androidx/media. New instance API, not deprecated static method, used. Existing pinned
  dependency, no new license/dependency or upgrade. Platform extractor remains first probe.

## Color decisions
If platform tags are missing, attempt a bounded8-second, worker-only Media3 container/codec
probe. Only a single video format with matching geometry can enrich unknown fields.
Known conflicts fail closed; HDR presence is unioned, never discarded. Failure leaves
UNKNOWN unchanged, cancellation releases retriever. Resolution is never a color heuristic.

If tags are still unavailable, default stays strict. Editor Input opens a bilingual
explicit preview-only interpretation warning. User may choose missing primaries=Rec709,
missing transfer=SDR, missing source range=limited. Explicit other primaries, PQ/HLG,
other transfer and HDR static metadata cannot be overridden. Known full range is preserved.
On decoded buffers, only in this opt-in mode, missing fields inherit the admitted source
interpretation; known values remain checked against expected format on every rendered
buffer. Null output formats, dimensions, changes/conflicts remain rejected. Reset with
Tagged only. Override lives only in the current dialog, not project/history/export settings.
Effective source is a separate copy; original tags retained for diagnostics. Viewer visibly
labels assumed Rec709 SDR; even Before retains that warning. No claim of source accuracy,
HDR conversion, exact10-bit path or export support. Normal export guards remain unchanged.

## Interaction / layout
Six chromatic rim stops match native screen axes: R right, M lower-right, B lower-left,
C left, G upper-left, Y upper-right. Native grading math is unchanged. Add native neutral
pixel tests across all6 directions and all4 wheel groups, plus palette direction tests.
This proves hue bias direction, not equality of every scene pixel with a swatch. Existing
relative no-jump drag retains0.55 gain and no parameter inertia. New artwork is original.
Quiet chromatic rim, shaded interior, vector and contrast puck improve hierarchy. Cards
remove4 duplicate Channels buttons and reduce wheel height148→128dp; shared Channels tab
remains. Numeric rows reduce vertical padding8→2dp and margins8→4dp, preserving48dp touch
areas. Primary/Channels numeric controls get animated press/changed-value feedback, never
animated parameter values. Animation-off/detach/cancel cleanup respected. Ready status row
collapses; Input and compact Play share toolbar. Full source/error detail remains available.

## Evidence required
Local Make6/native directional pixel tests,341 bilingual/3theme/17labels and27Python policy
tests PASS. Kotlin interpretation/palette regression source added; execution pending.
Android compile/lint/device tests pending. User phone acceptance remains mandatory: actual
missing-tag recovery, explicit opt-in/revoke, HDR refusal, visible paused/playing changes,
Before, background/resume, small screen, wheel hue/feel and Primary feedback. No readiness
credit from subjective polish or host passes;25%/75% unchanged.


## Implementation verification follow-up
Strict production JNI g++ build with conversion/sign warnings and actual JVM/JNI using
SHA512-verified Kotlin2.3.21 PASS. Exhaustive108 metadata tuples exercise opt-in refusal,
known-value preservation, HDR union, conflict rejection and decoded full-range inheritance.
This caught an sRGB-transfer eligibility mismatch with the native video contract; fixed
before handoff (only UNKNOWN/SDR_VIDEO are eligible, never overriding known sRGB).
Six-hue palette rankings and native24 group/direction neutral-pixel cases PASS. Native
color ASan+UBSan PASS;342 bilingual/3theme/17labels PASS. Numeric cancel/multitouch now
ends capture; new UI regression source added, execution pending. Assumed interpretation
warning persists for Before and the standalone draft title.

Official Media3 1.8.1 MediaFormatUtil.getColorInfo was also inspected: invalid platform
color integers are normalized to NO_VALUE there. Our independent mapping recognizes only
exact unset sentinels0/-1/null as UNKNOWN (not SDR); other unsupported codes remain OTHER.
Added Android source regression confirms unset tags still fail strict native admission.
No claim that the user's actual integer tags are0/-1: bytes/probe report still unavailable.


Opt-in interpretation is explicitly forwarded through the public MediaCodecVideoRenderer
changeVideoSinkInputStream hook to the video graph (official1.8.1 implementation inspected),
not just used to satisfy admission. Existing bit-depth fields are preserved, not promoted to
proof. Known graph-input conflicts still invalidate the gate. Default mode forwards the
original Format unchanged. Old video surface is hidden during recreate/failure until a new
first-frame event; video readiness is no longer confused with an available reference bitmap.
Assumption warning stays visible during adjustment as well as Before and ready states.


## Final candidate / failure history
734b947 published as fresh-install-41635353326. Android41635353326 and Portable41635353307
PASS. Assembly/lint/UI-source compilation/alignment/publication complete; emulator/UI test
execution/install-over skipped. Earlier41634026846 failed Kotlin list type inference;
7914660 added explicit Format list types.41634635509 then caught suspicious indentation in
the fallback block;734b947 corrected it without disabling lint. No failed build is delivered.
GitHub release API size8,067,959; digest6f0b24abcbea744f93cc56b687bd81569b7ec87df77f4feb9c612e79ddb1884f.
No local binary download/rehash claimed. User-source metadata/recovery, real graded frame,
10-bit output and subjective polish remain phone acceptance work, not CI results.
