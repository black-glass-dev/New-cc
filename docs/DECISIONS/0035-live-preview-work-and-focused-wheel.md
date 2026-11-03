# ADR0035 — Non-starving preview work and a focused wheel workspace

Date:2026-11-03. Implementation candidate; Android/device verification pending.

## Evidence / diagnosis boundary
User reports previous734b947 UI improved but live preview does not work while using wheels;
UI still messy. No new source/frame capture or device trace supplied. Code review proves
trailing debounce re-posts every move, so continuous movement can indefinitely delay work.
CPU completion also discarded frames whenever a newer revision existed, allowing display
starvation during continuous edits. These are deterministic code defects, not a measured
explanation of every phone/GPU symptom. Tiny paused seeks may also be player/codec no-ops.

## Research / design plan
Reviewed official Resolve18 Colorist Guide interface sections (viewer separate from primary
controls), https://documents.blackmagicdesign.com/UserManuals/DaVinci-Resolve-18-Colorist-Guide.pdf.
Reviewed publicly indexed Node Video interface screenshot from its Softonic listing: clear
viewer + one active tool, not four competing tools. No proprietary asset/code imported,
no exact layout or product identity copied. Research images remain ignored local scratch.

Original design: keep viewer outside controls scroll; one large active180dp wheel with four
48dp selectors, shared Primary/Wheels/Channels navigation; no duplicated cards or persistent
instruction block. Start in Wheels. Grade switch shares navigation row. Unified surfaces for
numeric rows, narrower readouts but48dp fine-step targets. Explicit sans-serif hierarchy,
compact reset/refresh icon buttons with localized accessible descriptions. Advanced RGB
controls remain available. No grading sliders, typing requirement or new dependencies.

## Work contracts
PreviewPulse schedules once and never moves an existing deadline as pointer events arrive.
Latest snapshot is read when pulse runs (video66ms/still80ms; not an FPS promise). CPU has
one worker plus one pending latest request. Completed valid-source frames are shown in
monotonic revision order even when newer work is pending, rather than discarding all progress.
Last queued settings are still rendered. Old-source/inline-transition/closed results are not
published. Before stays usable once a CPU frame exists; editing returns to After and enables
the grade so adjustments cannot be hidden behind comparison/bypass. Explicit bypass without
subsequent edits still saves disabled grade as requested.

GPU PreviewWorkQueue admits one active immutable shader recipe and one latest snapshot.
Each recipe acknowledges through its own once-only successful GL draw callback, posted to
main, checked by request token + player generation. Subsequent changes do not cancel active
frame work. Stale callbacks after close/background/cancel cannot complete new requests.
User Play/Pause is separate from transport pump state. Paused edits set volume0, seek around
stable bounded anchor and temporarily decode until the requested shader draws, then pause.
This avoids relying solely on a1ms seek while paused. Initial10s/subsequent5s draw deadlines
fail visibly and release resources; no infinite hidden playback on shader failure. Stop/
close remove pump/deadline callbacks, invalidate ownership and release codec/audio. Resume
recreates paused. This is preview transport only, no project timing/history edits. Frame
advancement around anchor is possible; not frame-exact frozen-cache replay. First reveal
still requires the player's first rendered frame AND a shader draw. GL draw acknowledgement
is not independent phone/display verification, performance measurement or output parity.
Known source/HDR gates and explicit preview-only Input interpretation remain ADR0034.

Play from a probed still editor starts inline playback using current draft (instead of
opening an immutable separate snapshot), allowing controls to remain reachable during play.
UI is no longer marked ready merely because update() returned; callback reports acknowledged
Before mode and pending state. Render/export paths without callback remain unchanged.

## Required verification
Pure production Kotlin pulse/queue regressions simulate10000 continuous moves, latest-only
coalescing, wrong/duplicate token, cancel/resume and terminal close; actual JVM/JNI suite PASS.
343 bilingual/3theme/17label checks PASS. Android UI regression source covers one visible
wheel, selector change and edits leaving Before; compilation/execution pending. Need actual
phone continuous drag paused/playing, latest release position, audio silence, deadline/error,
background/close during prepare, source/HDR refusal, small portrait/landscape and reduced
animation. Subjective polish still user acceptance. No10-bit/export proof or readiness credit;
25% complete /75% remaining.


### First-surface race review
Initial muted decode must continue until BOTH shader draw and player first-surface event;
pausing immediately on upstream GL draw alone could race the first visible output. Separate
10s surface deadline is canceled only by that surface event/release, not shader completion.
Subsequent paused requests already have a surface and pause on their own acknowledged draw.
This is implemented lifecycle ordering, not phone proof of visible output or audio behavior.
