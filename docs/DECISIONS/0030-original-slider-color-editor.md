# ADR0030: Original slider editor, bounded automatic preview, explicit saving

Date: 2026-10-24. Status: APK published, host policies pass; phone acceptance pending.

User reports typing-only controls, Apply seemingly inert, and generic live processing
failure. Reject proprietary UI/source copying; use original primary/RGB balance/tonal
groups and standard labelled sliders inspired by professional grading workflows.

## Changes

Remove17typing fields. Three groups expose all17native settings with numeric readouts,
accessible slider labels, reset/bypass and Before. Practical UI transport ranges expand
to include saved values without silent clamping. Reset keeps exact native identity values;
C++ still owns validity/GPU budget/color math, not slider interpolation.

Debounce still-preview changes220ms with at most one render in flight and one latest
pending intent. Revision mismatch never publishes stale results. Original source metadata,
CPU proxy budget, sRGB/ARGB checks, cancellation and no HDR/unknown fallback remain.

Supersede ADR0021/25's UI rule requiring a successful still before Apply. Saving validated
parameters must not depend on retriever/decoder availability. Apply validates current native
shader budget and calls existing exact session/clip-checked atomic history command. Failed
commit retains dialog and error; success explicitly says settings saved, NOT graded timeline
playback/export. This does not authorize rendering unknown/HDR sources. Source/preview errors
remain visible separately and must not silently disable saving valid intent.

Live renderer input-only output-format callbacks may have null raw MediaFormat before
codec output is available. Validate input descriptor there, but only run raw gate when raw
format exists; every actual rendered buffer still requires current known matching output
format through the unchanged per-buffer gate. Do not weaken raw color/geometry admission.
This repairs one premature-failure path, not proof of the user's exact failure cause.
Expose bounded nested exception diagnostics (max6causes/300chars each, cycle-safe) in local
selectable error text; never guess source tags or say all phone failures are fixed.

## Evidence / limits

308bilingual strings/3themes/17labels and whitespace pass. New pure Kotlin tests cover
34,017slider steps, expanded saved ranges and bounded/cyclic failures; not run yet in this
workspace (no host JDK/compiler). New instrumentation source covers no EditTexts, accessible
slider change and Apply despite missing decoder, plus commit rejection; not executed.
CI must compile all app/test sources and assemble fresh-install candidate. User owns phone
validation. Shipping grade-export guards stay, no parity/HDR/timeline claim.25%/75%.

## Executed evidence

Android40101757559 and Portable40101757571 at1f1bd21 PASS; real fresh-install release
fresh-install-40101757559 contains APK/checksum/metadata. APK8,008,831bytes; SHA256
89ff87e012a7f69fa5f6cfd1fbdec13b1e1bb3054e0fa5332fa3a3fedfef9adf.
Pinned Kotlin2.3.21 -Werror pure production controls test now actually passes34,017steps,
saved-range preservation, nested/long/cyclic diagnostics plus prior preview/projection
regressions. Restored reviewed host tools from PyPI/npm, verified compiler SHA512; no
binary/tool material in Git. UI interaction tests compiled, not executed. User's live
failure root cause still not identified from generic report; candidate improves one
premature failure path and diagnostic visibility, not blanket hardware/source support.
