# ADR0031: Fixed viewer and original precision workspace

Date: 2026-10-25. Status: APK published; phone visual/interaction acceptance pending.
Plan: docs/plans/COLOR_WORKSPACE_REDESIGN.md. New request has no attached screenshot yet;
phone report of blank frame and inconsistent controls is not independent device evidence.

## Presentation

Replace whole-dialog scrolling with full-window themed workspace, fixed Apply/Cancel,
fixed portrait viewer and separately scrollable grouped controls. Landscape uses side-by-
side viewer/controls. Keep system insets, source-time caption, compact status/Details,
Before/refresh/live toolbar, enable switch and selected tabs. Original ruler scrubber
subclasses SeekBar to retain keyboard/accessibility/touch-scroll handling; draws neutral
marker, active rail, ticks and cursor with shared palette/channel accents.48dp −/+ buttons
change exactly one slider step; native validation/math remain unchanged.

## Blank-viewer handling and strict color separation

Before this change, color admission ran before extraction. A rejection left no image,
and subsequent slider changes could obscure the reason. Show optional bounded paused
main-player reference only with same URI/current clamped position/ready surface checks;
label ungraded. Extract a separate bounded source frame and display platform reference
before grading admission. Unknown/HDR/incorrect bitmap may be DISPLAYED as ungraded
reference, not processed by native grader. This is ordinary viewing (like existing legacy
player), not an exception to native grade/live/export color policy or proof of color.
Only admitted source+bitmap may populate original/source render inputs. Supplied timeline
capture never enters native input. If no image exists show real empty/loading/error state.
Long diagnostic text is in a selectable child dialog, not allowed to consume viewport.
Preserve source failure while controls change. Reference is still image, NOT live video.

## Validation boundaries

325bilingual strings/3themes/17labels and diff whitespace pass. Added instrumentation
source for immediate reference display, no viewer ScrollView ancestor,17custom scrubbers,
one-step save; previous Apply-without-decoder/commit-refusal tests remain. Must compile
through actual Android job; not executed on phone/emulator. No dependency/assets copied;
no proprietary UI/code. Single in-flight/revision/cancellation/native/export guards remain.
No claim source mismatch/codec/GPU problems universally solved; actual phone visual,
landscape/font/insets/reference/drag/TalkBack acceptance pending. Readiness25%/75%.

## Executed checkpoint

Android40165432408 and Portable40165432394 at57879db PASS. APK fresh-install-40165432408
published,8,034,423bytes; SHA256
fec88f23d08b9465e5fbe8f382be6c496486445d100e232fa5d2e5b705ae0fa7.
325bilingual strings/3themes/17labels pass; new regression source compiled. Android build
success does not execute layout/touch/reference behavior. Official AOSP alert_dialog_material
layout inspected to check custom-panel integration; no proprietary resources copied.
No phone/emulator run or universal source compatibility claim. Documentation-only follow-up.
