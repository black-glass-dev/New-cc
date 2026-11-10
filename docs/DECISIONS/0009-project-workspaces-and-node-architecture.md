# ADR 0009 — Project workspaces and a typed node-processing boundary

Accepted direction, 2026-10-22. See [canonical baseline](../ADVANCED_ARCHITECTURE.md).

User requests an advanced node-based editing architecture, researched from public
Resolve and Node Video workflows, and applying it to the features already working.
They report the previous playhead APK works well on their own phone.

Decision: retain native editorial authority and existing proven Android preview/render
adapters. Add a local project shelf and managed per-project save/recovery paths,
contextual workspace panels without rebuilding playback surfaces, and an experimental
portable graph structure validator. Specify the advanced graph/evaluator, persistence,
time/automation, media, audio/color, scheduler and resource contracts before exposing
editable nodes. Current Flow view explicitly describes separate existing pipelines.

Alternatives rejected: copying either product's UI, a full immediate Activity/renderer
rewrite, Kotlin-only duplicate timeline/graph logic, pretend node controls, arbitrary
shader/expression loading, or changing document schema/export timing without gates.
No new dependency or backend is selected. Native graph validation is not rendering.

Consequences: existing document v1 preserved; app-private catalog has a separate label
and UUID. Native document ID is not changed by catalog naming/import. Shared evaluator,
editable nodes, graph serialization, coordinator extraction and durable journal remain
future phases. Catalog operations serialize process-wide, but project and metadata
writes are not one atomic transaction. Library is not an external backup. Explicit
recovery, Save As, URI relink and current export compaction consent remain necessary.

Changes to the baseline require an ADR plus compatibility/tests/rollout evidence.
