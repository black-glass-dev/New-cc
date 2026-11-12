# OwnCut মোবাইল এডিটর: ফিচার তালিকা, UI পরিকল্পনা ও আইকন ক্যাটালগ

**অবস্থা:** পরিকল্পনা; UI layout redesign এখনো করা হয়নি। এই নথি ব্যবহারকারীর পরের UI নির্দেশের জন্য প্রস্তুত। আইকনগুলো `StudioIcon`-এ নিজস্ব stroke-vector drawing হিসেবে যোগ করা হয়েছে, কিন্তু এখনই কোনো নতুন toolbar-এ বসানো হয়নি।

## ১. এখনকার ফিচার অবস্থা

### ব্যবহারযোগ্য পথে যুক্ত—তবে Android/device validation বাকি

| ফিচার | বর্তমানে যা আছে | গুরুত্বপূর্ণ সীমা |
|---|---|---|
| Trim, split, undo/redo, track actions | বিদ্যমান Edit/timeline controls | নতুন UI-তে প্রধান action হিসেবে অগ্রাধিকার দিতে হবে |
| Slip, Roll, Slide | native command/history, JNI, Kotlin action flow ও source-range preflight | ফোনে যাচাই হয়নি; সীমিত contextual dialog, direct manipulation নয় |
| J-cut/L-cut | detached audio ও source-range preflight | ফোনে যাচাই হয়নি; J/L waveform timeline gesture এখনো নেই |
| Voiceover | permission, AAC/M4A capture, stop-এ audio clip; প্রকল্পের পাশে `media/voiceovers`-এ কপি | app-private path; project package/export-সহ portability নয় |
| Jog dial | custom rotary scrub; release-এ seek commit | বর্তমান UI flow-তে ব্যবহারযোগ্য; ফোনে যাচাই হয়নি |
| Speed-ramp | keyframe persistence ও playback sampling; rich export-এ ≤২০০ms constant-rate segments, video speed effect + Sonic audio speed | piecewise approximation; clip duration/পরের edit alignment প্রভাবিত হতে পারে, exact continuous retime নয় |
| Offline smart reframe | bounded low-resolution frame sampling, NDK saliency/local-flow trajectory, aspect options, persisted pan keyframes ও rich-export crop; app UI/API/device acceptance বাকি | semantic person/object detection নয়; device behavior ও image quality অযাচাইকৃত |
| EQ / compressor / limiter / gate | per-clip persistence ও native DSP rich-export PCM path | live MediaPlayer audio-তে প্রযোজ্য নয়; UI-তে export-only কথাটি দৃশ্যমান থাকা চাই |
| Transform keyframes | persistence, interpolation, timeline marker ও preview/export path | নতুন compact keyframe editing UI দরকার |
| Text/subtitle overlays | timeline lane, duration block ও rich export burn-in path | ফোনে gesture/accessibility পরীক্ষা বাকি |

উপরের প্রতিটি ক্ষেত্রে **code path আছে মানেই verified/production-ready নয়**। Kotlin/Android build এবং device test এখনো হয়নি।

### এখনো যোগ হয়নি বা সম্পূর্ণ হয়নি

1. **Content-aware reframe:** অফলাইন NDK saliency + local optical-flow trajectory, aspect choices, transform keyframes ও rich-export crop path এখন যুক্ত হয়েছে। এটি semantic person/object detector নয়; Android/device tracking quality ও viewer/export behavior এখনো পরীক্ষা বাকি।
2. **Stabilization:** bounded offline sample → NDK optical-flow analysis → Smooth/Strong/Camera Lock → undoable transform keyframes → preview/rich-export path যুক্ত; Android/device validation ও visual-quality acceptance বাকি।
3. **Optical-flow retime:** engine API/algorithm থাকা shipped Android workflow নয়; decoder frames, compute budget, fallback, export integration নেই।
4. **Multicam:** synced angles/grid viewer/audio-sync এবং timeline angle switch UI ও persistent edit behavior নেই।
5. **Compound clip:** engine-এ ধারণা/operation আছে, কিন্তু app JNI/EngineSession/UI ও nested timeline workflow নেই।
6. **Adjustment clip:** track/clip effect-stack semantics, persistence এবং renderer integration নেই।
7. **Dual timeline:** overview + precision timeline একই playhead/selection/scroll state-এ নেই।
8. **প্রকল্প বহনযোগ্যতা:** voiceover কপি করা হয়, কিন্তু media collect/package, relink, missing-media recovery এবং portable export workflow নেই।
9. **Speed-ramp exactness:** বর্তমান segment approximation-কে exact continuous retime বা duration-preserving edit বলা যাবে না; adjacent clip/lane placement semantics সমাধান বাকি।
10. **Live audio FX:** EQ/dynamics-এর low-latency playback path নেই; বর্তমান DSP rich export-এ চলে।

## ২. মোবাইল-first UI পরিকল্পনা (পরের বাস্তবায়নের প্রস্তাব)

### Layout hierarchy

- **Viewer-first:** portrait ফোনে উপরের ~40% viewer; play/pause, source/timeline mode, timecode এবং fit/fullscreen controls viewer-এর প্রান্তে। Viewer-কে সাধারণ action toolbar দিয়ে চাপা যাবে না।
- **Transport strip:** viewer-এর নিচে frame-step, play/pause, jog-dial access, undo/redo এবং prominent timecode। Jog dial বড় modal না হয়ে bottom sheet-এ compact rotary control হবে; touch, keyboard ও accessibility seek একই exclusive-end policy ব্যবহার করবে।
- **Timeline:** transport-এর নিচে ruler + visible track-header gutter + horizontally scrollable lanes। Pinch zoom, zoom +/- এবং playhead follow বজায় থাকবে। Selected clip-এর জন্য contextual strip; অপ্রাসঙ্গিক সব action স্থায়ীভাবে দেখানো হবে না।
- **Action rail / contextual inspector:** ছোট, স্থির semantic icon rail; নির্বাচিত clip অনুযায়ী Inspector bottom sheet। Trim/Split/Undo/Redo সর্বদা দ্রুত; Slip/Roll/Slide, J/L cut, Speed, Reframe, Voiceover এবং Audio FX নির্বাচিত clip/track অনুযায়ী দেখাবে।
- **Landscape:** viewer ও inspector পাশাপাশি, timeline নিচে প্রসারিত; phone rotation-এ project/selection/scroll/dirty state অক্ষুণ্ণ থাকবে। Tablet layout আলাদা breakpoint হবে, ফোন UI-কে শুধু stretch নয়।

### Editing flows

- **Normal Edit (default):** viewer + transport + timeline; secondary actions “More” contextual sheet-এ। No noisy dashboard.
- **Trim mode:** clip-edge handle-এ ≥48dp hit target, source/timeline duration feedback, ripple state দৃশ্যমান; Roll-এ দুই clip-এর shared boundary, Slide-এ প্রতিবেশী clips দৃশ্যমানভাবে group highlight।
- **Speed mode:** timeline curve/keyframe overlay, keyframe add/remove/drag, min/max speed, preview status; export duration delta ও adjacent clip effect আগেই দেখানো। Approximation সতর্কতা export confirmation-এ থাকবে।
- **Reframe mode:** output aspect selector (9:16/1:1/4:5/16:9/4:3/3:2/2:1/2.39:1/5:4/9:21), custom even-pixel resolution capped at the UHD pixel budget, viewer crop frame, draggable subject target ও crop keyframes। বর্তমান tracker offline saliency + optical flow; semantic AI detector নয়।
- **Voiceover:** permission আগে; countdown/recording waveform ও elapsed timer; stop করলে preview/rename/keep/discard নিশ্চিতকরণ। Capture ফাইল প্রকল্প media folder-এ atomic copy; কপি ব্যর্থ হলে source রেখে actionable error।
- **Audio inspector:** EQ band editor, dynamics sections; export-only badge, bypass/A-B, reset; live playback integration যোগ না হওয়া পর্যন্ত “live effect” বোঝায় এমন icon/feedback নয়।
- **Color/FX:** existing feature identity বজায়; default Edit শান্ত, advanced color workspace ইচ্ছাকৃত entry point-এ।

### Interaction ও performance constraints

- সাধারণ gesture: tap-select, drag-move, pinch-zoom; এক gesture এক semantic কাজ। Drag-কে accidental horizontal scroll থেকে আলাদা করতে touch slop ও cancel-safe behavior।
- High-frequency preview updates coalesce; persistent edit/history কেবল gesture commit-এ। Native/session revision snapshot atomic; stale inspector action reject হবে।
- Timeline virtualization/culling painter order ও overlaps বজায় রাখবে; UI thread-এ media I/O/DSP নয়। Viewer overlay এবং icons draw cache-friendly; low-end device-এ waveform/thumbnail work bounded।
- Button target ≥48dp, icon-only action-এ localized content description + tooltip, visible focus, TalkBack label/state, reduced animation. English/Bengali strings format parity check.
- Theme palette roles `studio_themes.json` থেকে; icon contrast সব theme-এ যাচাই। Product-specific original geometry; DaVinci artwork/branding কপি নয়।

## ৩. আইকন ক্যাটালগ

`StudioIcon`-এ মূল existing glyph-এর পাশাপাশি নতুন original vector glyph প্রস্তুত:

- Editing: `SPLIT`, `TRIM_LEFT`, `TRIM_RIGHT`, `SLIP`, `ROLL`, `SLIDE`, `J_CUT`, `L_CUT`, `RIPPLE`, `UNDO`, `REDO`, `SPEED`, `SPEED_RAMP`, `KEYFRAME`, `FREEZE`, `JOG_DIAL`.
- Media/transport: `MEDIA`, `IMPORT`, `OPEN`, `SOURCE_VIEW`, `PLAY`, `FOLLOW`, `VOICEOVER`, `AUDIO`, `TRACK`, `DUAL_TIMELINE`.
- Audio: `EQ`, `COMPRESSOR`, `LIMITER`, `GATE`, `MUTE`, `BEAT`.
- Picture/effects: `REFRAME`, `TRANSFORM`, `STABILIZE`, `OPTICAL_FLOW`, `MULTICAM`, `COMPOUND`, `ADJUSTMENT`, `COLOR`, `PIP`, `TRANSITION`, `TEXT`, `SUBTITLES`.
- Project/navigation/accessibility: `HOME`, `EDIT`, `EXPORT`, `SAVE`, `SAVE_AS`, `RECOVER`, `SETTINGS`, `INFO`, `LOCK`, `MAGNET`, `MARKER`, `DUPLICATE`, `ZOOM_IN`, `ZOOM_OUT`, `CANCEL`, `PLUS`, `RENAME`, `REFRESH`, `FLOW`.

Glyphs কেবল visual affordance; ব্যবহারকারীকে action label ও TalkBack description দিতে হবে। Planned-but-unimplemented icon (`STABILIZE`, `OPTICAL_FLOW`, `MULTICAM`, `COMPOUND`, `ADJUSTMENT`, `DUAL_TIMELINE`) UI-তে enable করা যাবে না যতক্ষণ বাস্তব workflow যুক্ত না হয়।

## ৪. Design inspiration ও সীমা

DaVinci Resolve-এর official edit overview-তে dedicated edit/audio workspaces, 6-band EQ, stabilization controls, stacked/tabbed timelines এবং multicam workflow আছে। Cut-page dual timeline ও action-oriented toolbar-ও দ্রুত navigation-এর পরিচিত উদাহরণ। OwnCut-এর জন্য এগুলো থেকে **hierarchy এবং workflow separation** নেওয়া হবে; desktop density, proprietary icon shape, exact colors/layout বা branding কপি হবে না। ফোনে viewer ও primary timeline-কে অগ্রাধিকার দিয়ে advanced controls contextual রাখা হবে।

সূত্র:
- Blackmagic Design, DaVinci Resolve Edit page: https://www.blackmagicdesign.com/products/davinciresolve/edit
- Blackmagic Design, DaVinci Resolve 18.5 New Features Guide (Cut toolbar, timeline options/actions): https://documents.blackmagicdesign.com/SupportNotes/DaVinci_Resolve_18.5_New_Features_Guide.pdf?_v=1681801210000

## ৫. বাস্তবায়নের প্রস্তাবিত ক্রম (UI কাজ শুরু হলে)

1. Baseline UI map + icon/res string accessibility test; Viewer/transport/timeline hierarchy polish.
2. Contextual inspector navigation + compact action rail; Slip/Roll/Slide/J/L/Voiceover/Jog flows.
3. Speed-ramp editor + explicit approximate timing preview/guard.
4. Reframe/stabilization quality review on actual varied clips; only then consider semantic subject detector or additional tracking modes.
5. EQ/dynamics inspector + honest export-only state; then actual live DSP separately.
6. Separate architecture/API milestones for dual timeline, multicam, compound/adjustment and optical flow; do not expose placeholder buttons.

**বর্তমান সীমা:** এই turn-এ icon glyph catalogue ও পরিকল্পনা নথিই প্রস্তুত হয়েছে; existing screen layout বদলানো হয়নি, কোনো নতুন unimplemented feature action-ও সক্রিয় করা হয়নি।
