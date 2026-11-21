# ADR0026: Explicit alternate runner and original-signing preflight

Date: 2026-10-24. Status: source implemented; runner/signing execution pending.

GitHub Android40085082942 has zero steps; annotation reports failed payment/spending limit.
This is not compiler evidence. The integration receives403 listing secrets/variables;
configuration presence and original-key availability are unknown, not asserted missing.

Add manual choice github-hosted (default) / self-hosted. Only manual dispatch on the tracked
branch can select the dedicated self-hosted/linux/x64/owncut-build label set. Push and PR
never select that machine. Owner must provision a trusted isolated runner and review source;
this change does not register one or bypass account-wide Actions suspension.

Retain version assignment, original approved APK, certificate pin, final verification,
alignment and publication guards. New preflight exports only the public certificate from
existing keytool alias, with store password referenced by environment variable, compares
SHA256 to pin and approved APK signer, and checks reference package/lower version. Never
create a replacement key. Do not echo captured tool stderr or put passwords in argv.
Preflight cannot prove private-key password usability; final APK signing/verification remain
mandatory. Existing absent-signing builds can run checks but cannot publish a candidate.

Nine mock-based host policy tests cover matching identity, different key/reference/package,
nonincreasing/invalid versions, missing key, invalid pin and command failure. They are NOT
actual APK/certificate/device results. Existing7metadata/3install-over refusal tests pass.
No new library/tool dependency, actual self-hosted/Android/signing run or APK. Billing and
owner credential configuration cannot be changed through application source. Original
private-key loss cannot be repaired from the public APK; no uninstall/data-clear fallback.
See APK_BUILD_RECOVERY_BN.md for owner steps. Readiness remains25%/75%.
