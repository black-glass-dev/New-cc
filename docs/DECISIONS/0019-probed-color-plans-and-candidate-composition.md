# ADR 0019 — probed color plans and candidate composition

2026-10-23. Accepted for candidate implementation; shipping preview/export still guarded.

## Decision
Continue 1C with the missing input boundary and a shared immutable per-clip plan, not
an untested replacement of MediaPlayer or premature active-grade export enabling.

C++ validate_sdr_video_source owns admission policy. Canonical versioned JNI codes are
independent of Android/Media3 constants: primaries unknown0/Rec7091/other2; transfer
unknown0/SDR-video1/sRGB2/PQ3/HLG4/other5; range unknown0/full1/limited2, plus HDR-static
presence. Only explicit Rec709 + SDR-video + known full/limited and no HDR-static data
passes. Unknown, sRGB-tagged video, PQ/HLG/other primaries, invalid enum values or
contradictory SDR tags with HDR-static data refuse. This is admission, not conversion;
decoder/Media3 owns YUV/range conversion to ADR0018's normalized encoded RGB texture.

VideoSourceProbe is worker-only, local content/file URI only. It reads raw MediaExtractor
track formats; exactly one video track is required (no implicit multi-track selector).
Missing color keys remain UNKNOWN; no resolution/codec/sRGB/Media3-default inference.
Missing rotation defaults to the platform's unrotated convention, not a color default.
Metadata is detached; extractor releases in finally on every exit. Cancellation is
cooperative before/after setDataSource and between tracks, **not an interrupt of blocking
provider I/O**. Geometry/track identity and 0/90/180/270 rotation are validated by plan.

ColorRenderPlan captures an owned immutable clip list/revision, raw source metadata and
C++-generated grade recipe for each clip, deduplicating probes only within this request.
Check cancellation before/after probes/recipe generation and before publication; failure
returns no partial result and poisons no shared cache. Same source may have different
clip grades. No timeline/color math is duplicated in Kotlin. No cross-request URI cache.
Revision is a caller-supplied snapshot label, not proof the live model is unchanged.
URI bytes are NOT pinned/hash-verified: future live integration must validate actual decoder
format against the probe and address source replacement/permission changes, not reuse
this plan as proof of byte identity. A defaulted/inferred ColorInfo alone is not source
provenance; the old ColorInfo preparation overload is a synthetic adapter test helper.

Candidate wiring: shared timing planner retains the prior validation/explicit gap count;
candidate-only timing entry accepts active grades without changing shipping guards.
probeCandidate binds platform probe -> shared color plan -> per-clip GlEffect -> clipped
EditedMediaItems. Grade precedes global 720p Presentation. One explicit DEFAULT processor
factory is created per prepared plan and Candidate.configure attaches it to a Transformer
builder. Candidate keeps HDR (so an unexpected HDR path refuses in the effect), rather
than silently tone mapping a changed source. Existing ungraded export retains its prior
HDR tone mapping. No codec/output/preview starts in these candidate APIs, no UI route calls
them, and omitted gaps still require caller consent. Live preview must consume the same
plan and processing order before any shipping active-grade guard can be removed.

## Evidence and boundaries
Passed locally: six Make suites, Release CMake/CTest6/6, GPU/source ASan+UBSan; strict
production JNI build; actual JVM 200 metadata combinations + HDR-static refusal;
production Kotlin2.3.21 -Werror/native plan tests of independent grade recipes, dedup,
immutable output, source/geometry mismatch, unsupported tags, cancellation and failure.
Existing resource/patch gates pass. No shader math changed; ADR0018's software GPU result
is prior evidence, not a new Android/codec result.

Added Android ColorSourcePlanTest raw-tag mapping, candidate effect/clipping structure
and preserved shipping guards. **Not compiled/run here**, nor are probe provider I/O,
Media3 composition, codec/decoded-output, live playback, phone, performance or install-over
validated. Latest prior Android CI39950781623 did not start: GitHub billing/spending limit
annotation, not a compiler result. No APK. Verified roadmap remains25%/75%.
