#!/usr/bin/env bash
# Destructive reset only on a disposable CI emulator, before seeding. Never on a phone.
set -Eeuo pipefail
if [[ ${GITHUB_ACTIONS:-} != true || ${CI:-} != true ]]; then
  echo 'This reset/install test is restricted to GitHub Actions disposable emulators.' >&2
  exit 2
fi
mapfile -t devices < <(adb devices | awk '$2 == "device" {print $1}')
if [[ ${#devices[@]} != 1 || ! ${devices[0]:-} =~ ^emulator-[0-9]+$ ]]; then
  echo 'Exactly one disposable emulator is required; no device was reset.' >&2
  exit 2
fi
export ANDROID_SERIAL=${devices[0]}
if [[ ! ${OWNCUT_VERSION_CODE:-} =~ ^[0-9]+$ ]] || (( OWNCUT_VERSION_CODE <= 2 )); then
  echo 'A valid CI update version is required; no device was reset.' >&2
  exit 2
fi
cp app/build/outputs/apk/debug/app-debug.apk "$RUNNER_TEMP/current.apk"
./gradlew --no-daemon :app:assembleDebugAndroidTest :app:assembleDebug -PpatchVersionCode=$((OWNCUT_VERSION_CODE - 1))
adb uninstall com.owncut.app.test || true
adb uninstall com.owncut.app || true
adb install app/build/outputs/apk/debug/app-debug.apk
adb install -t app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb shell am instrument -w -e owncutUpgradeStage seed -e class com.owncut.app.UpdatePersistenceTest#seed com.owncut.app.test/androidx.test.runner.AndroidJUnitRunner | tee "$RUNNER_TEMP/seed.txt"
grep -q 'OK (1 test)' "$RUNNER_TEMP/seed.txt"
adb install -r "$RUNNER_TEMP/current.apk"
adb shell am instrument -w -e owncutUpgradeStage verify -e class com.owncut.app.UpdatePersistenceTest#verify com.owncut.app.test/androidx.test.runner.AndroidJUnitRunner | tee "$RUNNER_TEMP/verify.txt"
grep -q 'OK (1 test)' "$RUNNER_TEMP/verify.txt"
cp "$RUNNER_TEMP/current.apk" app/build/outputs/apk/debug/app-debug.apk
