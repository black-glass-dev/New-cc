# ADR0039 — Three-agent ownership and raw export evidence capability

2026-10-18 session date. Accepted; adapter implementation/device validation incomplete.

User asks to continue implementation and add two agents with a chat and retained division
of work. Coordinator owns Android/export/build integration; native worker owns new curves
files; QA worker owns new tests/qa and Android qa test files. No shared checkout/index writes,
root-doc/build/schema ownership stays coordinator. Separate Arena sessions use their assigned
branches; no switches/renames or force pushes. PR/patch handoff, coordinator review before
integration. Plan: docs/plans/THREE_AGENT_WORK_PLAN_BN.md. Workers not actually started here.

Export step: introduce RawVideoOutputFormatProvider, an optional capability implemented by
our future raw-evidence-preserving Codec. Returns MediaFormat for the currently held buffer
and associated format epoch, owner thread only, no dequeue/advance. GuardedCodec consumes
exactly once inside terminal native validation. Explicit valid crop normalizes through
existing C++/JNI helper. Missing/malformed/HDR/unknown color/query failure cannot fall back
to a valid-looking lossy Format. Drop does not query; both render release overloads validate.
Without capability existing strict Format behavior remains; production route/guard unchanged.
Diagnostics bound admitted-format examples to8 and retain total count.

This is a capability/guard integration seam, NOT an implementation of MediaCodec, proof of
buffer/source identity or completed graded export. DefaultDecoderFactory does not implement
it. No reflection/private Media3 code or Graden source/assets/dependencies are introduced.
Next: own public-API codec backend, epoch/lease management, real candidate output validation.

Synthetic Android regression source covers padded crop, two release overloads, changed crop,
missing/partial/raw query failure, HDR/unknown color and terminal no-fallback. These are not
actual codec/surface/export evidence. Record compile and execution outcomes separately.
Readiness25%/75%; user-phone live preview success retained, export unresolved.
