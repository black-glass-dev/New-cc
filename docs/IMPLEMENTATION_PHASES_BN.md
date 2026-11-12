# Owncut — বাকি কাজ ও ধাপভিত্তিক বাস্তবায়ন

আপডেট: ২০২৬-১০-২৩। ব্যবহারকারীর সিদ্ধান্ত: **প্রথম ধাপে কালার গ্রেডিং**;
যেখানে যৌক্তিক সেখানে C++20 অগ্রাধিকার। এই তালিকা বর্তমান বাকি কাজের নির্ধারিত
scope, কোনো পূর্ণ পেশাদার editor ইতিমধ্যে তৈরি হয়েছে—এমন দাবি নয়। ADR 0016 আগের
controller-first কাজের ক্রম বদলায়; reliability ও compatibility শর্ত বাদ দেয় না।

## বর্তমান অবস্থা

- Basic import/play/edit/history/render/Gallery, project library, বাংলা/English ও themes-এর
  আগের ব্যবহারকারী-স্বীকৃত baseline আছে। Advanced editing পুরোপুরি তৈরি হয়নি।
- Native ownership/recovery/checkpoint/paging-এর সাম্প্রতিক উন্নতি host-tested;
  সম্পূর্ণ Android acceptance ও busy-recreation/controller কাজ এখনো বাকি।
- Flow এখনো read-only। Editable/rendered node graph নয়।
- যাচাইকৃত weighted roadmap **২৫% সম্পন্ন / ৭৫% বাকি**। নতুন code লেখা ও ব্যবহারযোগ্য,
  যাচাইকৃত feature সম্পন্ন করা আলাদা। নিচের ধাপগুলো সমান ওজনের নয়।
- মোবাইল পরীক্ষা ব্যবহারকারী করবেন। Host test-কে ফোন/GPU/codec test বলা হবে না।

## ধাপ ১ — কালার গ্রেডিং: প্রথম অগ্রাধিকার

### ১A — C++ processing ভিত্তি: এই ব্যাচে বাস্তবায়িত, host-tested

- [x] Exposure, RGB channel balance, contrast/pivot, saturation।
- [x] Per-channel lift/gamma/gain, strength/mix এবং bypass।
- [x] নির্দিষ্ট sRGB ↔ linear transfer; linear Rec.709/sRGB primaries-এ হিসাব।
- [x] বাস্তব RGBA8 pixel processing; source/alpha/padding preservation।
- [x] Input/buffer/stride/overlap validation ও cooperative cancellation।
- [x] C++ RGB/luminance histogram; standalone versioned grading preset।
- [x] Narrow direct-buffer JNI; Kotlin-এ শুধু immutable settings ও platform adapter।
- [x] Production Kotlin → JNI → C++ pixel test, native unit tests ও sanitizer।

**সীমা:** এটি CPU reference/frame processor। এখনো live preview, video export,
project document বা undo stack-এ তখন grade সংযুক্ত ছিল না; ১B-তে native সংযোগ হয়েছে। RGB balance calibrated Kelvin/tint
নয়। Histogram পূর্ণ waveform/vectorscope নয়। HDR/YUV/premultiplied bitmap input নয়।

### ১B — Project, clip এবং history সংযোগ: বাস্তবায়িত, host-tested; Android acceptance বাকি

- [x] Stable clip ID-তে grade সংযুক্ত করা; future graph Color node-ও একই engine ব্যবহার করবে।
- [x] Grade edit/reset/bypass/copy/paste-এর native command ও undo/redo।
- [x] Versioned project persistence ও migration; পুরোনো project-এর neutral grade default।
- [x] Checkpoint/recovery, relink/reorder/split/delete ও independent projects-এর সঙ্গে পরীক্ষা।
- [x] Preview/export-এর জন্য immutable grade snapshot DTO/API প্রস্তুত; actual grading consumption ১C-তে।

**সীমা:** নতুন save schema v2; পুরোনো app v2 খুলতে পারবে না। Undo stack session-local;
grade values ফাইলে থাকে। Internal API আছে, live controls নয়। Active grade export এখন
refuse করে, preview স্পষ্টভাবে ungraded বলে; Android পরীক্ষা ও ১C integration বাকি।

### ১C — Color-managed preview ও export (চলমান, 2026-10-23)

- [x] Native explicit sRGB/linear709/Media3-SDR709 texture profile, original GLSL ও CPU oracle।
- [x] Software GLES-এ ৯৬ case ×৫১২ pixel comparison; সর্বোচ্চ RGB পার্থক্য ১/২৫৫,
  alpha/neutral/bypass/zero-strength হুবহু। এটি ফোন/Media3/codec proof নয়।
- [x] Candidate effect+explicit factory ও metadata/HDR rejection source যোগ হয়েছে;
  Android compilation/চলমান preview/export connection **এখনো হয়নি**।
- [x] Raw local-source metadata probe, native admission ও immutable per-clip color plan source।
- [x] Candidate composition-এ clip grade → global720p Presentation এবং explicit factory binding source।
- [x] Native expected/decoder-output verifier ও per-render terminal gate; candidate decoder hook source।
  Host/JNI/Kotlin tests পাস; actual Android codec/thread/pixel পরীক্ষা নয়।
- [x] Opt-in live clip preview source: shared GPU factory, raw decoder gate, clip loop/seek/audio/Before।
  এটি isolated experimental player; সাধারণ timeline নয়, Android build/runtime প্রমাণ হয়নি।
- [x] Native frame-error diagnostics; actual encode/decode ও cut/bypass comparison test source।
  Host metrics পাস; Android test চালানো হয়নি, codec tolerance এখনো প্রস্তাবিত।
- [ ] Probe/candidate-এর Android compilation ও actual decoder/codec validation; live route এখনো যুক্ত নয়।


- [ ] Android input color metadata অনুযায়ী explicit transfer/primaries conversion।
- [ ] C++-কেন্দ্রিক GPU processing; CPU reference-এর সঙ্গে pixel tolerance পরীক্ষা।
- [ ] Live preview এবং actual encoded export-এ একই grade/order/working-space contract।
- [ ] Unsupported HDR/codec/format স্পষ্টভাবে প্রত্যাখ্যান; ভুল sRGB assumption নয়।
- [ ] Seeking, playback, cancellation ও lifecycle-এ effect/resource ownership।

### ১D — ব্যবহারযোগ্য grading interface

- [x] ১৭ parameter draft, native still preview, Before/After, enable/reset এবং atomic Apply source।
- [x] বাংলা/English input, history/autosave wiring; host pixel/draft test পাস।
- [ ] Android UI/build/device validation; এই preview স্থির proxy, live graded playback নয়।


- [ ] পরিষ্কার Color workspace; exposure/contrast/saturation ও channel controls।
- [ ] Lift/gamma/gain controls, reset, bypass ও before/after comparison।
- [ ] Curves, HSL/secondary selection ও LUT support: native implementation ছাড়া fake control নয়।
- [ ] বাংলা/English, theme parity, accessibility ও uncluttered timeline/preview।

### ১E — Scopes ও acceptance

- [ ] Histogram UI; native waveform/vectorscope, documented signal/scale।
- [ ] LUT/curve bounds, precision ও malformed-preset validation।
- [ ] Neutral grade অপরিবর্তিত; saved project reopen ও undo/redo সঠিক।
- [ ] Color chart/known samples-এ preview এবং decoded export-এর মিল পরীক্ষা।
- [ ] Android build/ABI checks; compatible APK-এ ব্যবহারকারীর grading/phone test।

**ধাপ ১ শেষ হবে যখন:** UI-তে করা grade save/reopen/undo হবে এবং বাস্তব preview ও
export-এ একই ফল দেবে। শুধু এই ব্যাচের CPU engine-কে পুরো ধাপ শেষ বলা হচ্ছে না।

## ধাপ ২ — Editor, project ও media শক্ত করা

- Busy save/load/recovery চলাকালে Activity recreation ও writer-lease ownership ঠিক করা।
- Controller/task ownership, stale UI callback ও cancellation গুছিয়ে নেওয়া।
- Native document/catalog/recovery acknowledgement-এর transaction/failure semantics।
- প্রকৃত interruption/I/O tests; diagnostic receipt-কে পূর্ণ WAL বলে না চালানো।
- Stable asset identity, unused imports/bin, missing/offline media ও grouped relink।
- Project exchange/archive-এ media/font/preset reference policy ও migration।
- বড় timeline-এর সত্যিকারের bounded UI consumption; শুধু draw culling নয়।
- Professional editorial commands: snapping, ripple/roll/slip/slide ও multitrack edit,
  যেখানে বর্তমান basic commands এগুলো বাস্তবায়ন করে না।

**শেষের শর্ত:** project/history/media হারানো ছাড়াই load/edit/recover/switch/rotate;
ব্যর্থ operation নিরাপদে ফিরে আসবে; storage/provider edge cases documented।

## ধাপ ৩ — কার্যকর node editor

- Node registry, parameter schemas, typed ports ও versioned graph persistence।
- Identity/source/color/transform/output node-এর real evaluator।
- Original editable Flow: connect/disconnect, inspector, selection ও undo/redo।
- Cycle/type/size/budget validation; unknown-version preservation।
- Timeline clip/composition graph binding এবং preview/export একই evaluation contract।
- Dirty propagation, cache invalidation ও resource-aware scheduling।

**শেষের শর্ত:** node বদলালে প্রকৃত preview ও export বদলাবে এবং project reopen-এ graph থাকবে।

## ধাপ ৪ — Multitrack compositing

- একাধিক video/image layer-এর সত্যিকারের concurrent composition।
- Position/scale/rotation/crop/anchor এবং explicit canvas/resolution।
- Opacity/blend, mask/feather, layer order ও clipping।
- Gap/black frame/audio silence-এর নির্দিষ্ট rendering; consent ছাড়া সময় compact নয়।
- Alpha ও color-space correctness; প্রয়োজনমতো transition engine।

**শেষের শর্ত:** overlapping timeline-এর নির্দিষ্ট, পরীক্ষিত rendered output।

## ধাপ ৫ — Animation ও motion tools

- Native keyframe tracks, interpolation ও curve/spline editing।
- Effect/transform/color parameter animation ও easing।
- Title/text, font handling, graphics ও motion presets-এর original system।
- Speed changes/ramping, reverse/freeze ও source-time mapping।
- পরে tracking/stabilization: বাস্তব algorithm/capability যাচাই ছাড়া feature দাবি নয়।

**শেষের শর্ত:** animation timing preview/export-এ মিলে এবং editable data হিসেবে save হয়।

## ধাপ ৬ — Audio engine

- Sample-clock timeline, multitrack mixer ও A/V synchronization।
- Gain/pan/mute/solo, fade/crossfade এবং meters।
- Buses, native effects/automation; resampling/channel-layout policy।
- Callback-safe allocation/locking rules; underrun, latency ও cancellation tests।
- Preview এবং encoded output-এর audio duration/content মিল।

**শেষের শর্ত:** অডিও track/effects নির্ভুল সময়ে mix/export হবে; silent failure নয়।

## ধাপ ৭ — Advanced color, proxy ও performance

- High-bit-depth/float processing, wide gamut, explicit working/output transforms।
- HDR PQ/HLG/tone-map validation; hardware না থাকলে capability সীমা জানানো।
- Advanced scopes, camera calibration/shot matching ও color-management refinement।
- Proxy generation/association, bounded caches ও quality-independent preview।
- GPU/decoder/encoder scheduling, memory-pressure handling ও fallback policy।
- বাস্তব device-এ frame drops, thermal behavior, RAM ও export speed-এর evidence।

**শেষের শর্ত:** নির্দিষ্ট device/capability-তে মাপা নির্ভরযোগ্যতা; universal 4K/HDR দাবি নয়।

## ধাপ ৮ — Camera ও নিজস্ব `.ccraw`

- আগে format specification: version/header/metadata/size limits/integrity/migration।
- Sensor/CFA, black/white level, orientation, color matrices ও timestamp policy।
- Android camera capability/permissions/lifecycle; native processing যতদূর উপযুক্ত।
- Capture/storage, decoder/debayer, color conversion ও media pipeline integration।
- Malformed file defense, reference samples, calibration ও supported-device matrix।

**বর্তমান বাধা:** `.ccraw` format ও reference samples এখনো নির্ধারিত নয়। আগে design
proposal ও সিদ্ধান্ত; নামমাত্র extension লিখে RAW support ঘোষণা করা হবে না।

## ধাপ ৯ — Update delivery ও release completion

- পুরোনো approved APK-এর সঙ্গে compatible signing identity বজায় রাখা।
- Full downloadable APK, increasing versionCode, checksum ও accurate metadata।
- পুরোনো project/theme/language/media/history migration ও install-over validation।
- Build/lint/native/Android-source checks; emulator কেবল explicit opt-in।
- ব্যবহারকারীর ফোন পরীক্ষার সমস্যা ঠিক করা; phone result না পাওয়া পর্যন্ত pass দাবি নয়।
- Security/input fuzzing, dependency/license/notices ও release reproducibility review।

**বর্তমান বাধা:** original signer/owner configuration এবং আগের CI billing/access
সমস্যা unresolved। নতুন random key, uninstall বা data clear কোনো সমাধান নয়।

## কাজের নীতি

C++20: grading/curves/scopes, document/history, graph, timeline/compositor/animation,
audio ও reusable processing। Kotlin: Android lifecycle/UI/permissions/storage/codec
adapters এবং সরু JNI seam। নতুন library নিলে আগে maintenance/license/API/ABI/size review।
প্রতি batch-এ code, tests ও evidence; advanced feature-এর বদলে decorative UI নয়।
