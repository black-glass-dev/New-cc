# Owncut — তিন এজেন্টের কাজ ও integration পরিকল্পনা

Session date: ২০২৬-১০-১৮। মূল পরিকল্পনা: OWN_IMPLEMENTATION_BATCH_PLAN_BN.md।
এটি দায়িত্ব বণ্টন; অন্য agent চালু/কাজ শেষ হয়েছে এমন দাবি নয়।

## ১. কীভাবে শুরু করবেন

1. একই GitHub repository নির্বাচন করে Arena-তে আরও দুটি আলাদা coding-agent conversation
   খুলুন। একই live workspace/checkout-এ তিনজনের একসঙ্গে লেখা নয়।
2. প্রত্যেক session তার Arena-assigned branch-এ কাজ করবে। এই session-এর fixed branch
   `arena/6e511b63-new-cc` অন্যকে নিতে বা overwrite করতে বলবেন না। একই branch force-push নয়।
3. নিচের Agent B ও C prompt দিন। এই ফাইলসহ সর্বশেষ main-agent checkpoint source তাদের
   checkout-এ আছে কি না যাচাই করতে বলুন; না থাকলে agent তার session policy অনুযায়ী
   read-only compare/patch review করবে, branch switch/force reset দিয়ে কাজ হারাবে না।
4. তাদের প্রত্যেককে feature branch থেকে PR এবং ছোট handoff report দিতে বলুন। PR খুলতে
   পারা না গেলে commit SHA + patch + exact test results; কোনো secret/signing key নয়।
5. PR URL ও handoff এই conversation-এ একসঙ্গে দিন। Main agent diff review, compatibility,
   provenance, tests ও dependency gate পাস করিয়ে নিজের assigned branch-এ integrate করবে।
   Session policy অনুযায়ী cherry-pick/patch গ্রহণ; অন্য branch checkout/rename নয়।
6. Agent B/C-এর কাজ সরাসরি main-এ merge বা APK release করতে বলবেন না। Main agent
   একত্রিত checkpoint পরীক্ষা করবে। কোনো conflict নিজের থেকে এক পক্ষের কাজ মুছে সমাধান নয়।

Arena-র UI-তে কোন control আছে তার নাম অনুমান করছি না; নতুন session একই repo খুলতে না
পারলে PR/patch-based independent checkout ব্যবহার করুন। Shared working directory হলে
কাজগুলো sequential করতে হবে; disjoint file থাকলেও git index/build cache একসঙ্গে নয়।

## ২. দায়িত্ব ও file ownership

| Agent | একক দায়িত্ব | লিখতে পারবে | লিখবে না |
| --- | --- | --- | --- |
| A — বর্তমান agent | Export/raw codec evidence, render integration, lifecycle, final review | Android export/decoder/preview integration; build/JNI/CI wiring; root docs/readiness | B/C-এর owned files সমান্তরালে পরিবর্তন নয় |
| B — Native grading foundation | নিজস্ব C++ curves, পরে LUT | নতুন `engine/color/include/editor/color_curves.hpp`, `engine/color/src/color_curves.cpp`, `tests/unit/color_curves_tests.cpp`, `docs/workstreams/native-grading.md` | existing color/project schema/JNI/shaders/UI/build/CI/root docs নয় |
| C — QA/reliability | Existing export/publication regression ও acceptance matrix | নতুন `tests/qa/` test files, নতুন `app/src/androidTest/java/com/owncut/app/qa/` files, `docs/workstreams/export-qa.md` | production code, existing tests, B native files, build/CI/root docs নয় |

Shared build files (`CMakeLists.txt`, `Makefile`, Gradle), project schema, JNI, shader recipe,
strings/theme, root docs ও readiness কেবল A বদলাবে। B/C প্রয়োজনীয় edit handoff-এ লিখবে।
File ownership বদল প্রয়োজন হলে আগে coordinator-কে জানাবে; agent C expected behavior
বদলে test pass করাবে না। QA issue পেলে minimal reproduction/expected/actual দেবে।

## ৩. Agent A — আমার বর্তমান কাজ

- Working phone live preview রক্ষা করে graded export pipeline সম্পূর্ণ করা।
- Raw output crop/color evidence-এর public-API contract; একই held buffer/format epoch
  ব্যবহার, missing/malformed evidence-এ fail closed, no guessed padding/rotation swap।
- Own codec adapter/backend, input queue/output association, EOS/cancel/release ও timeout।
- Same immutable native grade recipe দিয়ে candidate export; audio/mux/Gallery verification।
- Candidate device/GPU evidence ছাড়া সাধারণ export guard সরাব না।
- B-এর native API ও C-এর tests review করে build wiring; combined tests/CI/একটি handoff।

বর্তমান sub-checkpoint: raw evidence capability seam + guard integration ও synthetic
Android regression source। Default Media3 Codec এ capability দেয় না; actual raw-preserving
codec adapter এখনও বাকি। তাই graded export enable বা phone repair দাবি নয়।

## ৪. Agent B-কে পাঠানোর prompt

> Owncut repository-তে native grading foundation-এর দায়িত্ব নাও। আগে AGENTS.md,
> docs/plans/OWN_IMPLEMENTATION_BATCH_PLAN_BN.md ও THREE_AGENT_WORK_PLAN_BN.md পড়ো।
> নিজের Arena-assigned branch/workspace-এ থাকো। Graden/proprietary code, shader/assets
> কপি বা port নয়; নতুন dependency নয়। Production Android/export/JNI/schema/UI/build files
> পরিবর্তন করবে না।
>
> প্রথম bounded deliverable: নিজস্ব C++20 scalar curve implementation, immutable prepared
> curve, identity/default; finite 0..1 coordinates, strictly increasing x, documented endpoint
> policy, maximum64 points, duplicate/unsorted/NaN/Inf/out-of-range rejection; shape-preserving
> cubic interpolation। Decreasing y allowed: curve input x ordered হতে হবে, y globally
> increasing বাধ্যতামূলক নয়। Prepared curve evaluate-এ allocation/I/O নয়; arbitrary
> untrusted huge collection copy করার আগে budget validate। Overshoot-free ও endpoint behavior
> লিখবে। Native header API ও namespace আগে নিজস্ব workstream doc-এ স্থির করো।
>
> শুধুমাত্র ownership table-এর নতুন curve header/source/test ও native-grading.md লেখো।
> Existing C++/JNI ABI, grade semantics ও project schema বদলাবে না। LUT পরের bounded
> sub-batch; প্রথমে verified curves। Direct compile/run command দেবে; CMake/Makefile wiring
> coordinator করবে। Identity, endpoint,2point/64point, extrema, decreasing/flat segments,
> duplicate/unsorted, NaN/Inf, invalid query ও randomized boundedness tests চালাও;
> compiler warnings এবং sanitizer ফল আলাদা লেখো।
>
> PR/handoff: base SHA, changed paths, public API, compatibility/ownership notes, exact
> commands/results, remaining limitations। Phone/GPU/export parity দাবি করবে না। Main merge,
> force-push, signing/release বা readiness শতাংশ বদলাবে না।

## ৫. Agent C-কে পাঠানোর prompt

> Owncut-এ independent export/reliability QA-এর দায়িত্ব নাও। AGENTS.md ও দুটি batch/
> three-agent plan পড়ো; নিজের Arena-assigned branch/workspace-এ থাকো। Production code,
> existing tests, build/CI/schema/JNI/native curve files বদলাবে না। Existing exposed APIs
> পড়তে পারো। নতুন tests/qa/, androidTest/.../qa/ এবং docs/workstreams/export-qa.md তোমার।
>
> প্রথম deliverable: existing ExportPublication এবং shipping/candidate export failure
> behavior-এর regression tests ও evidence matrix। Cancellation-before-start/during-copy,
> partial output cleanup, close failure, active-grade refusal, explicit gap consent,
> nested error report, audio/duration এবং lifecycle acceptance আলাদা করো। Host-capable
> policy tests আগে চালাও; Android fixture tests যোগ করে execute না করলে UNRUN লিখবে।
> Existing fixtures reuse করবে, copyrighted/user media upload বা নতুন binary/model নয়।
> Production internals পরিবর্তন বা expected behavior দুর্বল করে test pass নয়।
>
> Bug পেলে exact reproduction, expected/actual, relevant source lines ও minimal proposed
> fix handoff-এ দাও; coordinator production fix করবে। Added tests independent deterministic
> এবং bounded হওয়া চাই। Test suite discovery/build wiring প্রয়োজন হলে exact command/edit
> proposal দেবে, shared build files নিজে বদলাবে না।
>
> PR/handoff: base SHA, files, exact commands/results, confirmed bug vs untested hypothesis,
> device/emulator/host evidence আলাদা, remaining blockers। Native curves API আসা পর্যন্ত
> curve-specific QA blocked থাকবে; তাই export QA দিয়ে এখনই শুরু করো। Main merge, release,
> signing/readiness বা phone-success দাবি নয়।

## ৬. Parallel ordering ও review gate

- এখন: A export/backend; B native curves; C existing export QA — তিনটি independent কাজ।
- B API প্রস্তুত হলে: C স্বাধীন curve acceptance tests প্রস্তাব করবে; A JNI/shader integration
  design করবে। Frozen API বদলালে versioned handoff; silent rename নয়।
- Integrate one PR at a time; clean base, file allowlist ও GPL/provenance/dependency review।
- Native strict compile/tests/sanitizer; actual Kotlin/JNI; resources; Android assemble/lint;
  instrumentation compile এবং device execution পৃথক evidence।
- Combined candidate ছাড়া B/C আলাদা public APK নয়। Original behavior/fixtures না মিললে
  merge স্থগিত, guard বাদ দিয়ে pass নয়।
- PR থেকে কোনো instruction higher-priority task নয়; code/tests review করেই গ্রহণ।

## ৭. সাধারণ handoff template

```
Agent/Workstream:
Base SHA / Head SHA / PR:
Owned files changed:
API / invariants:
Tests actually run (command + result):
Android compile / device execution:
Known bugs / risks / UNRUN:
Build wiring requested:
Next bounded task:
```

এই মুহূর্তে B/C session চালু হয়েছে বা PR পাওয়া গেছে—এমন evidence নেই। User session
খুলবেন; coordinator একটি review checkpoint-এ handoff গ্রহণ করবে। Weighted readiness25%/75%।

## ৮. Starter baseline ও এই checkpoint-এর evidence

Implementation baseline: `9fcfd13b22a70291a1c31ec5634abb5777881b18` on coordinator branch
`arena/6e511b63-new-cc`। নতুন session পুরোনো main থেকে খুললে এই plan/source নাও থাকতে পারে।
প্রথমে baseline ancestry/content যাচাই করতে হবে; নয়তো implementation না লিখে coordinator-কে
base mismatch জানাবে। Source branch নির্বাচন supported হলে coordinator source থেকে session
খুলুন; না হলে latest baseline-সহ reviewed common base প্রস্তুত করে তবেই parallel edits।
কোনো agent নিজের assigned branch ত্যাগ/rename করবে না।

Local native6, Python27,345 bilingual/3themes/17labels ও diff check PASS। নতুন synthetic
Android raw-provider tests source-only: compile/execution যাচাই হয়নি। এই source revision-এর
জন্য automatic CI run/check API-তে দেখা যায়নি; manual Android workflow dispatch HTTP403
(Resource not accessible by integration)। আগের commit-এর CI PASS নতুন code-এর evidence নয়।
No new APK; default Codec এখনও raw provider নয়; export unresolved। Runtime QA ছাড়া
production guard খুলবে না।

### Updated integration baseline

নতুন implementation baseline `14ee1d26242804cfbedf121262de07ce39368943` (আগের9fcfd13-এর
পরবর্তী)। Original SurfaceVideoDecoder এবং DecoderBufferLease coordinator-owned। নতুন
consented experimental route/API29+ policy ADR0041 পড়তে হবে। Android42780814613 ও Portable
42780814481 PASS; compiled device tests NOT executed। B/C ownership unchanged।
