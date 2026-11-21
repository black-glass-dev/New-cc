# ADR 0011 — Quiet editor hierarchy and three restrained theme palettes

2026-10-22. The user's actual phone screenshots show the previous icon-only pass
still felt cluttered: diagnostic paragraphs between viewer and timeline, duplicated
import/play rows, too many equally prominent boxes and excessive timeline whitespace.
The user requests minimal, aesthetic, professional organization before the next roadmap
phase. Screenshot feedback is not replaced by a claim of visual approval of this build.

Research inspected (public official page text):
- https://m3.material.io/styles/color/roles — pair foreground/container roles; use
  accent emphasis according to importance, not arbitrary decoration.
- https://developer.android.com/design/ui/mobile/guides/styles/color — hue/chroma/tone,
  semantic consistency, restraint, tokens and contrast. Not a claim that an arbitrary
  complementary palette or our three themes have received Material certification.

Decision: use neutral, low-chroma large surfaces; reserve accent for selection/focus;
keep the red time marker stable and supplement selection with an outline. Three
original dark schemes (Graphite/ice, Midnight/periwinkle, Ember/amber) share semantic
roles. Authoritative palette tokens live in `studio_themes.json`, used by the app
and host contrast gate. Check text/muted/accent against surface/raised/selection at
4.5:1. These are UI themes, not video grading or color-management settings.

Normal portrait editor now uses fixed chrome with a fitted weighted viewer,
transport (Play, time, Follow, Import), one 112dp scrollable timeline, a focused
Undo/Split/Delete/Redo row and bottom workspace navigation. Metadata, original-ID
readouts, source-only slider, long help, secondary trim/ripple and project/recovery
controls belong to contextual Media/Tools panels. No full-width duplicate Play or
Import bars. Viewer/surface/native session is not rebuilt when panels change. A
scroll fallback is used on short windows/landscape or large font scale rather than
forcing a zero-height viewer. Timeline remains vertically scrollable for more tracks.

Routine status stays contextual; an important failure/recovery message also notifies
quiet Edit through Toast, using a bilingual severity-pattern bridge while existing
status sites are unstructured strings. This is a transitional seam, not a complete
structured diagnostic/event system. Repeated identical failures are deduplicated.
Export status/cancel remain in Deliver. Native/codec/provider error details may retain
provider language. Language and theme are persisted independently; Home Settings
exposes Appearance and language. Theme changes never mutate project/output data.

Buttons keep localized semantics and >=48dp targets, but no longer each have an
outlined filled box. Focus/selected outline remains. Confirmation text is retained.
No copied product UI/assets, new dependency, schema, codec/preset or node renderer.
Current Flow is still read-only: decorative nodes would misrepresent engine support.

Validation: palette/resource gate, host engine tests, Android layout/context/theme/
status regression plus existing language, real-player/edit/history/render/storage
suite. Actual phone ergonomics, user font override, TalkBack and aesthetic approval
still require phone validation. Current architecture/coordinator roadmap is unchanged.

Executed: `cb5bfa7`, Android `39777165014`, portable `39777165246` passed.
Verified APK/checksum: `phone-test-39777165014`. Physical aesthetic/ergonomic
review is still pending. See BUILD_AND_TEST for exact evidence and artifact limits.
