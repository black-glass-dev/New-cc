# নতুন APK তৈরির বাধা: কী হয়েছে এবং সমাধানের পথ

## Fresh-install exception (2026-10-24)

ব্যবহারকারী এখন এই test-এর জন্য fresh install অনুমোদন করেছেন। তাই original key ছাড়াই
আলাদা debug-signed APK প্রকাশের route যোগ হয়েছে; stable update-এর নিয়ম অপরিবর্তিত।
Uninstall-এর আগে project/media backup জরুরি; পরের update compatible হবে এমন নিশ্চয়তা নেই।


## Current build checkpoint (2026-10-24)

Repository public করার পর Android40095688463-এ assembly/lint/test-source compilation/ZIP
alignment পাস করেছে। নিচের billing-startup বাধার বর্ণনা ঐতিহাসিক; বর্তমান run শুরু হচ্ছে।
এখন workflow stable signing configuration পায়নি বলে candidate প্রকাশ করেনি। নতুন key
দিয়ে আগের অ্যাপের update নিশ্চিত করা যাবে না; original compatible key লাগবে। ADR0027।


তারিখ: 2026-10-24। এটি recovery guide; নতুন APK বা সফল build-এর ঘোষণা নয়।

## ১. Billing সমস্যা কোডের error নয়

Android run `40085082942`-এর job-এ কোনো step চালেনি। Check annotation বলছে recent
payment failure অথবা spending limit-এর কারণে job শুরু হয়নি। কোনটি আসল কারণ, owner-এর
billing page ছাড়া আলাদা করে নিশ্চিত করা যাচ্ছে না। কোড বদলে এই account restriction
সরানো, টাকা পরিশোধ করা বা account budget বাড়ানো যায় না।

**সর্বনিম্ন কাজের পথ — repository/account owner:**

1. GitHub-এ লগইন করে account Settings → Billing & licensing / Billing & plans খুলুন।
   Repository organisation-এর হলে সেই organisation-এর billing owner-কে করতে হবে।
2. Failed payment/invoice বা Actions budget/spending-limit alert দেখুন। প্রয়োজনীয় billing
   correction owner করবেন। শুধু limit বাড়ালেই পুরোনো failed payment ঠিক হবে, এমন নয়।
   অনুমতি ছাড়া agent কোনো charge বা paid-plan পরিবর্তন করছে না।
3. Repository Settings → Actions → General-এ Actions enabled আছে কিনা দেখুন।
4. Actions → **Android app and JNI** → **Run workflow** খুলুন। Branch:
   `arena/6e511b63-new-cc`; runner: `github-hosted`; `run_device_tests`: **false**।
   ফোনে পরীক্ষা ব্যবহারকারী করবেন; emulator দরকার নেই। Workflow যদি default branch-এ
   না থাকার কারণে UI-তে Run workflow না দেখায়, authenticated GitHub CLI দিয়ে:

   ```sh
   gh workflow run android.yml --repo islamyousuf032-glitch/New-cc \
     --ref arena/6e511b63-new-cc -f runner=github-hosted -f run_device_tests=false
   ```
5. Job steps শুরু হলে billing blockage সরে যাওয়ার প্রমাণ পাওয়া যাবে। এরপর compiler/
   dependency/signing error থাকলে সেগুলো আলাদা করে ঠিক করতে হবে; APK এখনই নিশ্চিত নয়।

## ২. বিকল্প: নিজের trusted কম্পিউটারে runner

Workflow-তে manual `runner=self-hosted` বিকল্প যোগ হয়েছে। এটি GitHub-hosted compute
ব্যবহার করে না। তবে বিদ্যুৎ/কম্পিউটার/ইন্টারনেট লাগে; account-wide Actions নিষিদ্ধ থাকলে
এই পথও কাজ নাও করতে পারে। কোনো runner এখানে provision বা চালানো হয়নি।

- Dedicated trusted Linux x64 machine ব্যবহার করুন; signing key থাকা ব্যক্তিগত দৈনন্দিন
  কম্পিউটারে অপরিচিত public repository code চালানো নিরাপদ নয়। Prefer isolated ephemeral
  runner; অন্য repository/PR-এর জন্য একই machine ব্যবহার করবেন না।
- Repository Settings → Actions → Runners → New self-hosted runner-এর **নিজস্ব নির্দেশনা**
  অনুসরণ করুন। Registration token কেবল সেই trusted machine-এ দিন; chat/source-এ নয়।
- Labels হবে `self-hosted`, `linux`, `x64`, **`owncut-build`**।
- Git, GitHub CLI, Python3, make, C++ compiler, curl/unzip এবং Java/Android setup actions
  চালানোর উপযুক্ত environment/permissions থাকতে হবে। GitHub/Google/Gradle/Maven dependency
  hosts-এ internet দরকার; যথেষ্ট disk space রাখুন। Pinned SDK36/build-tools36.0.0/
  NDK28.2.13676358/CMake3.22.1 workflow install করবে; applicable Android terms মানতে হবে।
- Manual dispatch-এ শুধু session branch নির্বাচন করে runner `self-hosted` দিন। CLI:

  ```sh
  gh workflow run android.yml --repo islamyousuf032-glitch/New-cc \
    --ref arena/6e511b63-new-cc -f runner=self-hosted -f run_device_tests=false
  ```

স্বয়ংক্রিয় push/PR hosted runner-এই থাকে। Self-hosted route কেবল explicit manual dispatch
ও নির্দিষ্ট branch-এ অনুমোদিত। Reviewer-কে dispatched source বিশ্বাসযোগ্য কিনা নিশ্চিত
করতে হবে। Fork PR approve করে বা arbitrary branch checkout করে key প্রকাশ করবেন না।

এই agent workspace-এ প্রয়োজনীয় Android SDK/NDK নেই; Google SDK/dependency hosts-এ outbound
access অনুমোদিত নয়। তাই এখানে শুধু `./gradlew` চালিয়ে বিকল্প local APK তৈরি সম্ভব নয়।

## ৩. Update-install-এর জন্য আলাদা signing শর্ত

Build চালু হওয়া এবং আগের অ্যাপের ওপর install হওয়া **এক জিনিস নয়**। দরকার:

- একই package `com.owncut.app`;
- আগের অনুমোদিত APK-এর সঙ্গে একই signing identity;
- বাড়ানো versionCode।

আগের debug APK runner-generated key ব্যবহার করেছিল বলে নথিতে আছে। আসল key যদি backup,
trusted machine বা repository secrets-এ সংরক্ষিত থাকে, সেটিই ব্যবহার করতে হবে। এই GitHub
সংযোগে secrets/variables listing **403** দিয়েছে; ফলে সেগুলো সেট করা আছে কি না এখানে
নিশ্চিত করা হয়নি। Secret-এর value GitHub থেকেও সাধারণভাবে ফেরত পড়া যায় না।

Owner-only configuration: `OWNCUT_KEYSTORE_BASE64`, `OWNCUT_KEYSTORE_PASSWORD`,
`OWNCUT_KEY_ALIAS`, `OWNCUT_KEY_PASSWORD` secrets এবং `OWNCUT_SIGNING_CERT_SHA256` public
variable। পূর্ণ নিয়ম: [UPDATE_APKS.md](UPDATE_APKS.md)। কোনো password/private key chat-এ
দেবেন না। এলোমেলো নতুন key বানিয়ে pin বদলালেও legacy update নিশ্চিত হয় না।

নতুন `verify_signing_identity.py` build-এর আগে key certificate, pin, approved release
`phone-test-39777165014`-এর signer/package/version মিলিয়ে fail-closed check করে। এটি public
certificate পরীক্ষা; private key password-এর কার্যকারিতা final signing-এ যাচাই হবে।
Final APK-তেও আগের signer/package/version/alignment check বজায় আছে। Missing signing
configuration থাকলে build checks হতে পারে, কিন্তু downloadable candidate publish হয় না।

**আসল private key হারিয়ে গেলে APK থেকে সেটি উদ্ধার করা যায় না। সেক্ষেত্রে uninstall ছাড়া
আগের installation update করার বর্তমান শর্ত পূরণ করা সম্ভব নয়।** সম্মতি ছাড়া uninstall,
data-clear, package বদল বা signing-policy শিথিল করা হবে না।

## ৪. APK কখন পাওয়া যাবে

Runner build চালাতে পারলে, source compile/sign/verification পাস করলে, এবং original signer
মিললে workflow `app-patch-…` GitHub Release-এ APK, checksum ও public metadata প্রকাশ করবে।
সেই নতুন APK সরাসরি দেওয়া যাবে। আপনি নিজের ও অন্যান্য ফোনে পরীক্ষা করবেন। Build checks
ফোনের acceptance-এর বিকল্প নয়; metadata-তে device tests `not-run` থাকবে।

বর্তমান অবস্থা: recovery code যোগ হয়েছে, mock-based9 signing policy tests পাস; actual
runner/signing/APK execution হয়নি। সামগ্রিক অগ্রগতি ২৫% সম্পন্ন / ৭৫% বাকি।
