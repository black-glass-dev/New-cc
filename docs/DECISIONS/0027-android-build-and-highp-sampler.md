# ADR0027: Android compilation and explicit highp texture sampling

Date: 2026-10-24. Status: compiled and host-GPU verified; phone/codec parity pending.

Repository now public. Push-triggered jobs execute; manual workflow dispatch still403 via
this integration. Android40094980849 exposed two actual Kotlin errors: Media3 C.NO_VALUE
is not defined (use Format.NO_VALUE); optional VisibleForTesting was unavailable (remove
annotation, retain internal candidate access and shipping guard). No dependency added.

Android40095260838 passed APK assembly/lint/test-source compilation/ZIP alignment. After
shader change, Android40095688463 atb802517 passed those same checks. Earlier historical
UNCOMPILED notes are superseded; instrumentation/device/codec tests remain UNRUN.

Portable40095527236 passed native CTest but Mesa llvmpipe25.2.8 reported sRGB case29,
byte1444 actual3/reference6 (difference3, gate2). Shader had highp floats but default
lowp sampler precision. Request uniform highp sampler2D explicitly in both identity
and grading shaders. Portable40095688482 atb802517 now passes native CTest and all96
production shader/CPU cases ×512pixels with the unchanged2/255 gate and exact alpha/
identity/bypass assertions. Do not infer Android GPU/encoded/live-display parity.
Add native regression assertions preserving sampler precision across all3encodings and
identity/nonidentity paths. Targeted native test and local Make6 pass. Diagnostics now
annotate host-GLES traceback; CPU CTest runs before GLES so failures stay independently
visible. No threshold relaxation or renderer/backend substitution.

Workflow warns stable signing not configured, skips candidate release/publication. Debug
assembly success is NOT a delivered update-compatible APK. No new private key, uninstall,
package change or guard removal. Owner must securely configure original compatible key;
unknown recoverability is not fixed by repository visibility. Readiness remains25%/75%.
