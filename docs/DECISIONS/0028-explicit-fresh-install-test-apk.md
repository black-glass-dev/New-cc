# ADR0028: Owner-authorized fresh-install test candidate

Date: 2026-10-24. Status: published fresh-install test candidate; phone acceptance pending.

User explicitly waived the prior install-over requirement for this test and will uninstall
and freshly install. Preserve the stable update path and its approved-signer guards; add a
separate debug fresh-install publication route. Android still requires an APK signature:
use the existing runner-generated debug signer, not an unsigned APK or a claimed legacy
compatible update. No private key is published or requested from the user.

Only a successful push on the tracked branch with explicit [fresh-install-apk] commit
marker and absent stable signing can publish this candidate. No automatic fallback on
ordinary pushes/PRs. APK assembly/lint/test-source compilation/native regression and ZIP
alignment remain mandatory. Verify actual APK signature, package, version and debuggable
flag before separate fresh-install-<run> prerelease publication. Include exact source SHA,
APK/certificate SHA256, temporary signer/no guaranteed update and unrun device-test flags
in fresh-install-metadata.json, not update-metadata.json. Eight mock policy tests pass.

Warn to Save As/back up projects and preserve media before uninstall; uninstall can remove
app-local projects/settings. Agent does not uninstall or clear any phone. Future signer
continuity not promised. User owns phone acceptance; grading/export shipping guards stay.
No device/codec/parity or completed-feature claim. Stable-key setup is not required for
this explicitly accepted fresh install, but remains necessary for guaranteed updates.

## Executed result

Android40096664665 and Portable40096664742 atf9f7464 PASS. Fresh-install prerelease
fresh-install-40096664665 contains actual verified debug APK (8,004,475bytes), checksum
and fresh-install-metadata.json. API confirms APK SHA256
965754bc673c8a1f034432982460d334ac94a16e4fbb66f1e6bc8abd195d45e7.
No phone/device/codec run; stable update path not used. Only docs changed afterwards.
