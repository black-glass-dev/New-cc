# Owncut — নিজস্ব implementation ও বড় ব্যাচের কর্মপরিকল্পনা

সিদ্ধান্ত: ২০২৬-১০-১৮ (session date)। মালিক: ব্যবহারকারীর নির্দেশনায় coding agent।
এই পরিকল্পনা execution order নির্ধারণ করে; পুরোনো নথির historical evidence মুছে দেয় না।

## ১. পণ্য ও ownership নীতি

- App-specific code, grading algorithms, UI ও assets নিজস্ব হবে। Graden-এর code,
  shader, parameter implementation বা assets কপি/port করা হবে না। কোনো GPL integration অনুমোদিত নয়।
- Public repository পরে private করা ও paid Play Store release-এর লক্ষ্য বজায় থাকবে।
  আগে প্রকাশিত code/license-এর অধিকার repository private করলে বাতিল হয় না।
- Android/Media3 ও অনুমোদিত tooling নিজস্ব লেখা নয়: তাদের license/notice রাখতে হবে।
  নতুন dependency আনার আগে maintenance, license, size, platform/API ও supply-chain audit।
- Commercial/closed-source উপযোগিতা চাই; restrictive copyleft code নয়। নতুন licensing
  সিদ্ধান্ত বা third-party ownership পরিবর্তন ব্যবহারকারীর অনুমোদন ছাড়া নয়।
- Graden-এর feature নাম inspiration/inventory হতে পারে; code বা proprietary parity নয়।
- C++20-এ domain math, validated processing, project/history। Kotlin-এ UI, Android
  codec/storage/lifecycle এবং narrow JNI transport। সব Android code C++-এ পুনর্লিখন নয়।

## ২. নিশ্চিত baseline ও বর্তমান blocker

- ব্যবহারকারী নিশ্চিত করেছেন ভিডিওর মধ্যে live preview এখন দেখা যাচ্ছে এবং ভালো চলছে।
  এটি user-phone evidence; সব device, HDR,10-bit output বা export parity-এর প্রমাণ নয়।
- ব্যবহারকারী export ব্যর্থতা জানিয়েছেন; পূর্ণ export error/ফাইল/device trace পাওয়া যায়নি।
- Audit: TimelineRenderExporter.composition active grade প্রত্যাখ্যান করে; production
  export এই route-ই ব্যবহার করে। Graded candidate আছে, সাধারণ route-এ wired নয়। এটি
  code-confirmed limitation, ব্যবহারকারীর প্রত্যেক export failure-এর নিশ্চিত কারণ নয়।
- Existing basic timeline/render/gallery/history/persistence baseline রক্ষা করতে হবে।
- Weighted readiness এখনও25% verified /75% remaining। Code লেখা মানেই verified নয়।

## ৩. কম input, বেশি কাজ: execution contract

প্রতিটি বড় ব্যাচে: audit/design → implementation → regression → docs → Android CI →
প্রযোজ্য হলে একটি candidate APK → স্পষ্ট evidence/handoff। স্বাভাবিক implementation
বাছাই agent করবে। প্রতিটি method/control-এর জন্য অনুমতি চাইবে না।

ব্যবহারকারীর input কেবল: destructive migration/data loss, license/product-scope change,
অপরিহার্য source/device evidence, অথবা বড় UX trade-off। Export failure report না থাকলেও
static audit, policy/transport, unit tests ও candidate preparation চালিয়ে যেতে হবে।

এক ব্যাচে সম্পর্কিত কয়েকটি উন্নতি হবে; unrelated feature একসঙ্গে ঢুকিয়ে regression
আড়াল করা নয়। Large batch মানে অপরীক্ষিত সব feature এক APK-তে দাবি করা নয়। কোনো কাজ
অসম্পূর্ণ থাকলে তার কারণ ও পরবর্তী action লিখতে হবে; background completion-এর প্রতিশ্রুতি নয়।

## ৪. বড় implementation ব্যাচ

### A — Export reliability এবং দৃশ্যমান failure diagnosis [চলমান]
- [x] Graded বনাম plain production/candidate route audit; preview phone success নথিভুক্ত।
- [x] Render/publish/document-copy phase, immutable clip/grade count ও720p preset report।
- [x] Nested error chain এবং version/revision-সহ selectable/copyable local report।
- [x] Active grade pending হলে encode/publication-এর আগে fail-closed preflight।
- [x] Candidate decoder crop transport audit; pinned Media3 crop-loss regression source যোগ।
- [ ] Crop evidence-preserving export adapter implementation এবং device acceptance।
- [ ] Cancellation, audio, mux validation, storage publication/cleanup failure matrix।
- [ ] Guarded graded candidate-এর actual codec/GPU পরীক্ষা; তারপর production admission।
Acceptance: কোনো ungraded substitution নয়; explicit error অথবা সত্যিকারের validated file;
Gallery partial row/file cleanup; user-authorized gap compaction; no silent timing changes।

### B — Shared preview/export grade contract
- Versioned immutable native recipe, transfer/processing order ও defaults এক জায়গায়।
- Identity, bypass, primary/wheel extremes, alpha, bounds, crop/rotation ও mixed clips।
- Saved-project compatibility; old grade silently reinterpret বা clamp নয়।
- Same recipe/shader contract দিয়ে candidate export; documented tolerance/fixtures।
Acceptance: real rendered output comparison, duration/audio verification; preview-only
interpretation কখনো source evidence বা normal-export admission হবে না।

### C — সম্পূর্ণ primary grading workspace
- Exposure, temperature/tint, contrast/pivot, saturation/hue, lift/gamma/gain/balance,
  master/channel controls, strength, bypass/reset ও useful feedback।
- Existing controls audit করে কোনটি implemented/verified, কোনটি নতুন তা আলাদা inventory।
- Original compact UI, fixed viewer, বাংলা/English, themes, accessibility; grading sliders নয়।
Acceptance: save/reopen/undo/redo, paused/playing feedback, export equivalence, no jumping।

### D — Curves এবং LUT
- নিজস্ব bounded monotone curves: RGB/custom, পরে hue/sat/luma relationships।
- Documented .cube subset parser: strict bounds, size, NaN/Inf, overflow, malicious file budget।
- Native interpolation, strength/bypass; shader parity; versioned persistence/history।
Acceptance: identity/known fixtures, malformed file refusal, GPU tolerance, export test।

### E — Scopes ও measurement
- Existing native histogram reuse; waveform, RGB parade ও vectorscope পর্যায়ক্রমে।
- Scope input source, encoding/range, sample budget ও refresh throttling স্পষ্ট।
Acceptance: known patches/ramp, clip/channel bounds, no UI stall; scope pixel evidence,
not calibrated display/10bit output proof। CIE scope পরে সঠিক color-management ভিত্তিতে।

### F — Color management ও advanced grading
- Evidence-driven input metadata, explicit CST, gamut/transfer, tone/gamut mapping।
- Log/zone controls, RGB mixer, masks/secondary qualification, film/detail/noise tools
  পৃথক supported capability হিসেবে; motion/temporal tools-এর resource budget আগে।
Acceptance: numeric references, unknown/HDR refusal policy, no guessed Rec709/HDR,
source4K10bit থেকে output10bit inference নয়। RAW/.ccraw specification আলাদা সিদ্ধান্ত।

### G — Editing/performance/lifecycle hardening
- Multi-clip split/cut/trim/delete/reorder/history/relink/project recovery বজায় রাখা।
- Codec/surface ownership, pause/resume, seek, cancellation, thermal/memory limits।
- Optimize measured hotspots only; proxy never silently replaces full-quality source।
Acceptance: repeatable long-session/storage/lifecycle matrix; no content/timing drift।

### H — Paid release readiness
- Signing/key custody, update continuity, migration/backup/restore; Play policy/storage/privacy।
- Dependency notices, provenance/SBOM, release reproducibility, crash/error support।
- Target device matrix, encoder capabilities, feature honesty ও final source/license audit।
Acceptance: full install/update/recovery tests ও user sign-off; debug signer নয়।

## ৫. পরীক্ষার স্তর ও checkpoint evidence

1. Native tests + sanitizer/strict compile যেখানে সম্ভব।
2. Actual production Kotlin/JNI tests; fake Android stub-কে runtime proof নয়।
3. Resource/language/theme + Python policy tests।
4. Android assemble/lint/instrumentation-source compile।
5. Instrumentation/emulator চালালে আলাদা ফল।
6. ফোনে playback, grade, audio/export, gallery, lifecycle: ব্যবহারকারীর evidence আলাদা।

প্রতি ব্যাচ শেষে record: changed scope, source revision, passed/unrun/blocked, APK link
থাকলে digest/signing warning, remaining work ও acceptance status। বহু ছোট APK নয়।
Readiness বাড়বে verified acceptance অনুযায়ী, line-count বা সময় অনুযায়ী নয়।

## ৬. প্রথম execution checkpoint

এই turn-এ A-এর audit, preflight এবং report plumbing বাস্তবায়ন শুরু/সম্পন্ন হয়েছে।
এটি graded export enable বা ফোন export repair হয়েছে এমন দাবি নয়। Candidate route ও
পরবর্তী C++/GPU কাজের acceptance পূর্ণ না হলে production guard বজায় থাকবে।

### A: audit follow-up
Media3 1.8.1 DefaultCodec.convertToFormat → MediaFormatUtil.createFormatFromMediaFormat
raw width/height বহন করে, explicit crop বহন করে না। Padded synthetic fixture দিয়ে
Android regression source যোগ হয়েছে; candidate guard blind padding অনুমান করবে না।
এটি shipping grade guard-এর অতিরিক্ত candidate limitation, ফোনের raw output মাপ নয়।
পরের বাস্তবায়ন: public API-তে raw crop evidence ধরে রাখার codec adapter/design; reflection
বা নিজের থেকে2176→2160 অনুমান নয়।

## ৭. যাচাইকৃত প্রথম checkpoint

Implementation source: `a82d563a669a987807ff9b59e1bc633d9a7f28a7`।
Android CI `42772350310`: assemble/lint/instrumentation-source compilation/APK alignment PASS।
Portable CI `42772350391`: PASS। Local native6, actual production pure Kotlin export-report
regressions, Python27,345 bilingual/3themes/17labels ও diff checks PASS।
Instrumentation/emulator/phone export execution হয়নি; crop regression source compiled,
not executed। Public fresh-install APK publish করা হয়নি—diagnostics-only ছোট APK নয়।
Shipping graded export এখনও blocked। Next: crop-evidence-preserving candidate adapter +
real output verification; তারপর reviewed production admission, never blind guard removal।

## ৮. তিন এজেন্টে execution

[দায়িত্ব, file ownership ও copy/paste prompts](THREE_AGENT_WORK_PLAN_BN.md)। A: export/
Android/integration; B: নতুন native curves; C: independent export QA। B/C এখনও চালু হয়নি।
A-এর পরের sub-checkpoint: optional raw-current-buffer capability guard-এ যুক্ত; default
Media3 backend এটি দেয় না। Real crop-preserving codec adapter ও device acceptance বাকি।

## ৯. Original backend implementation checkpoint

SurfaceVideoDecoder ও DecoderBufferLease বাস্তবায়িত; candidate factory raw-preserving
backend বেছে নেয়। Native normalization + guard বহাল। Input/output/EOS/owner thread ও
cleanup code এবং10000-epoch pure production regression PASS। Real Android drain test
source যোগ হয়েছে; execution বাকি। API29+ explicit capability;26–28 fallback নয়।
পরবর্তী gate: Android compile/actual decoder drain + real graded output/parity/audio/
Gallery acceptance। তার আগে production grade guard নয়। B/C কাজ/পুরো roadmap সম্পূর্ণ নয়।
