# ADR0040 — Original candidate surface decoder and held-buffer epochs

Session date2026-10-18. Implemented candidate backend; Android/device acceptance pending.

Continue the export workstream rather than another diagnostics-only checkpoint. No workers
or external grading code are assumed. Public Codec interface pinned1.8.1 is the contract:
https://github.com/androidx/media/blob/1.8.1/libraries/transformer/src/main/java/androidx/media3/transformer/Codec.java
Android public MediaCodec configure/dequeue/getOutputFormat/release APIs are used; no reflection
or private/derived upstream implementation. Dependencies and license obligations unchanged.

SurfaceVideoDecoder owns a synchronous Android MediaCodec on the creator thread, with one
input lease and one output lease. Own bounded DecoderBufferLease tracks format epoch/held
index, input EOS, output EOS and terminal close/failure. No output dequeue happens while a
buffer is held; raw evidence and converted Format are associated with that held epoch.
Native crop normalization determines visible size in converted Format; raw evidence remains
available to GuardedCodec for every render release. No padding inference or source resizing.

Initialization rejects protected/unknown/HDR sources and unsupported rotation; candidate
requires API29+ to configure KEY_ALLOW_FRAME_DROP=0. This is an explicit narrower capability,
not a silent API26 fallback. Hardware/software selection follows Android's regular decoder
list; no advertised hardware-only guarantee. Platform config/profile/device quirks are not
claimed complete. Audio factory and shipping plain export remain unchanged.

Input references are checked, repeated pending input queries do not clear existing payload,
EOS carries no bytes. Output config buffers drop, empty EOS releases before completion,
nonempty EOS becomes complete only after held output release. Event polling is bounded16
per call; poll waits are zero; frame timestamps convert with checked arithmetic. Every
exception in active codec operations terminalizes lease. Release is idempotent and codec
release owns buffer cleanup; caller's Surface is never released by decoder. Initialization
failure releases codec. Candidate.configure opts into backend via GuardedDecoderFactory;
shipping graded route remains guarded until actual output acceptance.

Actual pure Kotlin tests:10000 format epochs, no format advance with held frame, invalid/
double release, input/EOS ordering, nonempty/empty EOS and terminal close/fail PASS. Added
real authored-video Android test source drains with repeated stable held-format queries,
drops output, and verifies EOS/idempotent release. This is not rendered GPU/color proof.
Existing GradedExportParityTest now reaches candidate backend when executed; execution
and phone output remain required. Full plan, curves/LUT/scopes/release are NOT complete.

Build checkpoint: source14ee1d26242804cfbedf121262de07ce39368943, Android42780814613 /
Portable42780814481 PASS. New backend/drain and guard test sources compiled, NOT executed.
Actual pure Kotlin lease/route tests, native6/Python27/resources348 PASS. APK published
fresh-install-42780814613 with temporary signer. No real device/GPU/export-parity claim.
