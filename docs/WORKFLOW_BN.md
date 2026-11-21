# Owncut: অ্যাপ ও কাজের প্রবাহ — নকশা v1

**তারিখ: ২২ অক্টোবর ২০২৬।** এটি পরিবর্তনযোগ্য ভিত্তি; সব advanced feature এখনই তৈরি নয়।
বিস্তারিত engineering চুক্তি: [Advanced architecture](ADVANCED_ARCHITECTURE.md)।
সিদ্ধান্ত: [ADR 0009](DECISIONS/0009-project-workspaces-and-node-architecture.md)।

## ১. অ্যাপ খুললে

প্রথম পর্দা **Projects**। এখানে আগে তৈরি/import করা project-এর নাম, শেষ খোলা/রাখার
সময় এবং recovery snapshot আছে কি না দেখা যাবে। নতুন project, পুরোনো `.owncut`
file import, project খোলা ও নাম বদলানো যাবে। তালিকা refresh করার ব্যবস্থাও আছে।

- **নতুন project:** একটি নাম দিন → আলাদা local project তৈরি হবে → editor খুলবে।
- **আগের কাজ:** তার card-এর “খুলুন” চাপুন → saved project editor-এ আসবে।
- **Project file import:** file বেছে নাম দিন → যাচাই করে নতুন local copy তৈরি হবে।
  বাইরের original file বদলাবে না। এই file-এর মধ্যে মূল ভিডিওগুলো প্যাক করা নেই;
  original media রেখে দিতে হবে, প্রয়োজনে relink করতে হবে।
- **নাম বদল:** library-তে দেখানো নাম বদলাবে; clip বা source file বদলাবে না।
- নতুন ব্যবস্থার আগে app-এর ভেতরে থাকা workspace থাকলে original না মুছে আলাদা
  entry-তে নেওয়া হবে। Corrupt file চুপচাপ মুছে/খালি project দিয়ে প্রতিস্থাপন হবে না।

Local Projects তালিকা কোনো cloud backup নয়। Uninstall বা data clear করলে এটি এবং
media access grant হারাতে পারে। গুরুত্বপূর্ণ কাজ বাইরে **Save project as…** করুন,
মূল ভিডিও রাখুন। Debug APK signature mismatch হলে backup ছাড়া data মুছবেন না।

## ২. Project খুললে

উপরে project name ও **Projects-এ ফেরার** বোতাম। তার নিচে workspace বিভাগ। Preview,
playhead, timeline এবং selection একই session-এর অংশ; বিভাগ বদলালে নতুন করে editor
বা video surface তৈরি হয় না। ছোট পর্দায় নিচের controls দেখতে scroll করা লাগতে পারে।

| বিভাগ | এই ধাপে যা কাজ করে |
|---|---|
| **Edit** | video import, timeline/playhead, split, trim, move, delete, gap/ripple delete |
| **Media** | import, নির্বাচিত source তথ্য, source relink |
| **Flow** | বর্তমান preview ও export route-এর read-only ব্যাখ্যা; editable nodes নয় |
| **Deliver** | বর্তমান MP4 render, cancel এবং Gallery publication |

Undo/Redo, Save, saved state খোলা, Save As ও explicit recovery সাধারণ project controls।
বর্তমান time ruler/editor grid 30 fps; export-এর source cadence আলাদা। এখনই project
FPS, 4K, HDR বা RAW slider যোগ করা হয়নি, কারণ শুধু UI দিয়ে এগুলো সত্যি চালু হয় না।

## ৩. একটি সাধারণ কাজ

১. Projects → নতুন project → নাম দিন।
২. Edit → ভিডিও নিন → preview প্রস্তুত হলে Play করুন।
৩. লাল playhead/ruler দিয়ে জায়গা বেছে split/trim/move/delete করুন। ভুল হলে Undo।
৪. প্রয়োজন হলে Media-তে source পুনরায় নির্বাচন/relink করুন।
৫. Deliver-এ MP4 render করুন। বর্তমান renderer একটি sequential video track নিয়ে
   720-high SDR H.264/AAC output দেয়; gap বাদ দেওয়ার জন্য আপনার সম্মতি লাগে।
   Android 10+ এ Gallery-র Movies/Owncut-এ যায়; পুরোনো Android-এ file picker থাকে।
৬. **Projects** বা Android **Back** চাপলে পরিবর্তিত local project আগে save হবে,
   তারপর তালিকায় ফিরবে। Save ব্যর্থ হলে editor বন্ধ না হয়ে error দেখাবে।
৭. পরে একই project খুলে saved timeline-এ কাজ চালিয়ে যান। নতুন করে খুললে পুরোনো
   Undo stack ফিরে আসে না; rotation-এ চলমান session/history রাখার ব্যবস্থা আলাদা।

ফোনের Home button/হঠাৎ app বন্ধ/প্রসেস kill স্বাভাবিক Projects-এ ফেরা নয়। Debounced
recovery snapshot থাকতে পারে, কিন্তু শেষ মুহূর্তের প্রতিটি edit রক্ষার নিশ্চয়তা নয়।

## ৪. Recovery কীভাবে

Recovery badge মানে একটি snapshot আছে; সেটি saved primary project-এর সমান বা নতুন—
এমন নিশ্চয়তা নয়। Default-এ saved primary খোলে। দরকার হলে editor-এ **Recover autosave**
চাপুন ও প্রতিস্থাপনের সম্মতি দিন। পছন্দ হলে Save বা Projects দিয়ে local copy-তে রাখুন।
Recovery না নিয়ে নতুন edit করলে আগের recovery snapshot বদলে যেতে পারে। কোনো edit
না করে Projects-এ ফিরলে অযাচিত পুরোনো recovery চুপচাপ মুছে দেওয়া হয় না।

## ৫. আসল node-based editor-এর নকশা

দুটি model একসঙ্গে কাজ করবে:

- **Timeline:** কোন clip কখন চলবে, source-এর কোন অংশ ব্যবহার হবে।
- **Node graph:** সেই image/audio কীভাবে transform, mask, merge, color বা mix হবে।

উদাহরণ (ভবিষ্যৎ processing; এই build-এ এই effect chain চলে না):

```text
Source → সময়/clip range → Transform → Color → Output
                            ↑
                           Mask

দুটি source → Merge → Sequence output
Audio sources → Gain/Mix → Audio output
```

প্রতিটি node-এর typed input/output থাকবে। ভুল ধরনের wire, cycle, missing input ও
অতিরিক্ত resource চাহিদা reject হবে। ইতিমধ্যে C++-এ bounded graph structure যাচাইয়ের
ভিত্তি যোগ হয়েছে; এটি **renderer নয়**, file-এ graph save-ও করে না। বর্তমান preview
MediaPlayer এবং export Media3 আলাদা route-এই চলে। Flow view-তে তাই কোনো ভান করা
editable effect/wire নেই।

পরের ধাপগুলো: session coordinator আলাদা করা → graph format/registry → বাস্তব
source/transform/output evaluator → shared preview/export → compositing/masks/audio →
keyframes/curves/color → proxy/cache ও advanced tools। Custom `.ccraw`-এর জন্য format
specification ও sample দরকার। পূর্ণ পরিকল্পনায় performance, lifecycle, serialization,
color/audio time, cancellation, licensing ও testing gate নির্ধারিত আছে।

## ৬. পরে নকশা বদলাতে হলে

এই নথি ও engineering baseline update করব; বড় সিদ্ধান্তে নতুন ADR রাখব। Saved project
compatibility, output-এর পরিবর্তন এবং migration tests একই সঙ্গে বদলাবে। আগে কাজ করা
import/edit/play/render ভেঙে নতুন page/node যোগ করাকে অগ্রগতি ধরা হবে না।


## যাচাইয়ের অবস্থা

বর্তমান code `f652c38`-এর Android build/lint, alignment এবং পুরো emulator suite
পাস করেছে (`39748116540`); portable CMake/CTest-ও পাস (`39748116580`)। নতুন library
ও navigation এখনও আপনার ফোনে যাচাই হয়নি। [পরীক্ষার APK](https://github.com/islamyousuf032-glitch/New-cc/releases/download/phone-test-39748116540/app-debug.apk)।
ফোনে দুটি project আলাদাভাবে edit/ফিরে/reopen, recovery, rotation ও render/Gallery
বিশেষ করে পরীক্ষা করবেন। বর্তমান local library-কে external backup ভাববেন না।


## নতুন UI ও ভাষা (Studio UI ব্যাচ)

নতুন ইনস্টল/পুরোনো ব্যাচ থেকে এই UI-তে প্রথমবার এলে আগে **English / বাংলা**
নির্বাচন করতে হবে। পছন্দ রাখা থাকে। Projects-এর উপরের **gear icon** থেকে পরে
বদলাতে পারবেন। ভাষা বদলাতে project file বা original media পরিবর্তন হয় না।
System file picker/codec/provider-এর নিজস্ব ভাষা app-এর নিয়ন্ত্রণের বাইরে।

Editing, history, save/recovery, import/relink, playback ও delivery action এখন
icon দিয়ে দেখানো হয়। বোতাম ধরে রাখলে নির্বাচিত ভাষায় tooltip; accessibility
label-ও থাকে। Workspace navigation-ও icon, বর্তমান বিভাগে outline। Project name,
confirmation, capability/read-only ব্যাখ্যা এবং language choice পড়ার মতো text-এই
রয়েছে—সম্মতি বোঝাতে শুধু অস্পষ্ট icon নয়।

Timeline-এ dark ruler, grid, আলাদা lane, source name, selected clip accent/handles
ও আগের red playhead আছে। সাজানোর জন্য fake waveform/thumbnail যোগ করা হয়নি।
এটি presentation update; node processing বা render preset-এর নতুন দাবি নয়।

Android run `39764813788` ও portable `39764813603` পাস। নতুন APK:
https://github.com/islamyousuf032-glitch/New-cc/releases/download/phone-test-39764813788/app-debug.apk
ফোনে দুই ভাষা, Settings, ছোট পর্দা/বড় font, tooltips, rotation এবং আগের render/Gallery
flow বিশেষ করে দেখবেন। ইনস্টলের আগে external project backup রাখবেন।


## Screenshot অনুযায়ী পরিষ্কার layout ও theme

স্বাভাবিক portrait Edit-এ এখন viewer → ছোট transport row → timeline →
Undo/Split/Delete/Redo। নিচে workspace navigation; header-এর Tools icon দিয়ে
trim/ripple এবং Save/Save As/Open saved/Recover পাওয়া যাবে। Source slider ও
বিস্তারিত metadata শুধু Media-তে; নির্দেশনা/সাধারণ status মূল Edit-এ নয়।
গুরুত্বপূর্ণ error/recovery notification থাকে। Deliver-এ render/cancel/status।
Short landscape/বড় font-এ layout scroll করার fallback আছে।

Home gear → Appearance থেকে Graphite, Midnight বা Ember বাছাই করুন। একই Settings
menu-তে Language আছে। এই রং শুধু app-এর; footage/render grading বদলায় না।
Flow এখনও read-only, কার্যকর node editor নয়—সেটি স্থাপত্যের পরবর্তী বাস্তবায়ন।

নতুন APK: https://github.com/islamyousuf032-glitch/New-cc/releases/download/phone-test-39777165014/app-debug.apk
Android `39777165014` এবং portable `39777165246` পাস; আপনার ফোনে নতুন layout/theme
এখনও যাচাই হয়নি। গুরুত্বপূর্ণ project externally Save As করে তারপর APK বদলাবেন।

## নতুন reliability ও update ব্যবস্থা — ২০২৬-১০-২২

Quiet layout/themes অনুমোদনের পর advanced architecture-এর Phase B শুরু হয়েছে।
একই managed project-এর দ্বিতীয় writer আটকানো, rotation-এ owner ধরে রাখা, বাইরের
পরিবর্তন দেখে primary overwrite বন্ধ করা, bounded snapshot থেকে project/recovery
খোলা এবং নষ্ট catalog metadata রেখে repair করার ব্যবস্থা যোগ হয়েছে। পুরো session
coordinator বা node renderer এখনো তৈরি হয়নি।

অগ্রগতির হিসাব `docs/plans/readiness.json`-এ ১০০টি weighted readiness point দিয়ে
রাখা হয়েছে। এটি কাজের ঘণ্টা বা সব professional feature-এর মাপ নয়; pending/blocked
কাজের জন্য কোনো point যোগ হয় না। `python3 scripts/progress.py` বর্তমান হিসাব দেখায়।

**APK নিয়ে জরুরি কথা:** পুরোনো build runner-generated debug key ব্যবহার করত। নতুন
APK-এর নাম “patch” করলেই update হয় না—আগের signing identity লাগবে। পুরোনো private
key পাওয়া না গেলে তার public certificate/APK থেকে সেটি উদ্ধার করা যায় না। তাই এখনকার
app uninstall/data clear করবেন না। স্থায়ী key, password এবং public certificate pin
শুধু repository owner-এর নিরাপদ GitHub Actions settings-এ সেট করতে হবে; chat-এ নয়।
নির্দেশনা: `docs/UPDATE_APKS.md`। এই integration-এ secrets management permission
HTTP 403 হওয়ায় এখানে তা করা যায়নি।

CI এখন কম version-এর APK-তে native project ও আসল language/theme preference রেখে
বেশি version `install -r` করে সেগুলো টিকে থাকে কি না যাচাই করে। স্থায়ী signing
configuration না থাকলে downloadable APK প্রকাশ করা বন্ধ। একটি runner-এর debug
update পরীক্ষা পাশ মানেই আপনার পুরোনো APK-এর signature মিলে গেছে—এমন দাবি নয়।

এই ব্যাচের প্রকৃত ফল: source 8a6aab4-এর build/lint/full emulator suite পাস; আলাদা
install-over পরীক্ষা ব্যর্থ। কারণ ধরতে নতুন diagnostic যোগ হয়েছে, কিন্তু পরের CI
job শুরু হয়নি—GitHub account payment/spending-limit error দিয়েছে। Owner-এর GitHub
Billing & plans/Billing & licensing দেখতে হবে; এটি app build error বা signing setup
একই সমস্যা নয়। কোনো GitHub password/token এখানে দেবেন না। মোট readiness **২৫% শেষ,
৭৫% বাকি**; নতুন কাজ পুরো checkpoint পাস না হওয়ায় এখনো অতিরিক্ত ৩ point যোগ হয়নি।
নতুন patch প্রকাশ হয়নি। এখনকার app অক্ষত রাখুন।

## পরবর্তী Phase B ব্যাচ: native session coordinator

C++-এ edit/history/save/load/recovery-এর owner আলাদা হয়েছে; JNI এখন সেটি ব্যবহার
করে। Revision-checked ২৫৬-clip page, failed edit rollback, Save As copy ও recovery
draft-এর পৃথক নিয়ম এবং concurrent edit/read পরীক্ষা যোগ হয়েছে। Local Make, CMake/
CTest, ASan/UBSan এবং official JNI header দিয়ে host syntax check পাস। Android-এর
নতুন bridge tests এখনো চালানো হয়নি; host check Android ABI/phone test নয়। পুরো
Activity controller ও durable journal এখনো বাকি। তাই অগ্রগতি **২৫% শেষ / ৭৫% বাকি**
অপরিবর্তিত; নতুন APK বা install-over সাফল্য দাবি করা হচ্ছে না।
