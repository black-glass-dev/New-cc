# ADR 0016 — Reprioritize color grading first, native processing first

2026-10-23. User explicitly requests a phased remaining-work list with color grading
in phase one and C++ used wherever reasonable. IMPLEMENTATION_PHASES_BN is the active
sequence; ADVANCED_ARCHITECTURE remains the product direction, not the old work order.

Start with a genuine C++ SDR frame processor/reference and a narrow stateless JNI
bridge, not decorative sliders. Color math, parameter validation, pixel layout safety,
histograms and preset serialization are native. Kotlin owns immutable field transport
and Android platform integration, not duplicate grade math. Reuse this module for
future persisted Color nodes and shared preview/export evaluation.

COLOR_PIPELINE documents the original formulas, finite ranges, transfer/primaries,
straight-alpha input, saturation weights, clipping and rounding. Do not call RGB
channel balance Kelvin/tint. Do not feed raw YUV/HDR/premultiplied bytes to this adapter.
Standalone presets do not silently change .owncut v1 or imply per-clip persistence.

Phase 1A is implementation/host-tested only. Next: native grade commands and versioned
per-clip persistence/recovery; explicit color conversion/shared preview/export GPU
processing; live controls, curves/LUT/secondary/scopes; actual Android and encoded-output
parity. Existing MediaPlayer preview and Media3 rendering do not yet run this grading.
No UI claims, color readiness credit, APK or realtime/hardware support without evidence.

This reprioritizes, not removes, busy lifecycle/controller, bounded media/UI, transactions,
editable graph, compositor, animation, audio, HDR/performance, .ccraw and update delivery.
Full nine-phase scope/exit criteria are in the Bengali plan. Approved-signer continuity,
non-destructive editing, explicit timing consent, bilingual/themes and user-owned phone
testing remain required. Roadmap weights unchanged: color becomes in_progress, with
no new verified credit. No new runtime dependency, proprietary source/assets or copied UI.
