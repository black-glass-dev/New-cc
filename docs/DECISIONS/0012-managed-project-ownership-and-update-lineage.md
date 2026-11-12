# ADR 0012 — Single writer, guarded checkpoints and durable APK identity

2026-10-22. User accepted the quiet UI and requests substantial planned engine work,
percentage reporting and APK patches installable over the previous app.

Start architecture phase B with a managed storage seam rather than pretend to ship
an effects renderer. Android owns filesystem leases/SAF; C++ remains project codec,
time and edit authority. Managed sessions acquire a file lock on a worker, carry the
same owner through configuration retention, and release after final engine shutdown
(or successful explicit return to Home before another editor can reopen).

Checkpoint verifies a bounded SHA-256 file fingerprint, performs native atomic save,
validates a fresh native load, then clears recovery. A cooperative second editor is
refused. A changed primary is not blindly overwritten. External Save As may succeed
even if the managed checkpoint conflicts: report that distinction, keep dirty state
and recovery. Corrupt recovery is validated in a temporary native session before
replacing current state. This is not a full journal/revision-CAS against arbitrary
writers ignoring locks, durable history or complete EditorCoordinator extraction.

Catalog repair is explicit and validates native primary before rewriting metadata;
old metadata/bak is preserved separately. Unknown future versions are refused.
Corrupt native documents are not replaced with empty projects. Recovery data is not
silently promoted to primary. Catalog/project updates are still separate transactions.

APK delivery must use a persistent signing identity and increasing versionCode.
Do not distribute random-signer debug APKs as install-over patches. See UPDATE_APKS
for fail-closed CI, same-job N−1→N install tests, pinning and the unavoidable old-key
limitation. GitHub secret-management permission is blocked in this session; no private
material enters chat, Git or release assets. Configuration, not a fictional success,
is required before publishing.

Percentage metric: `docs/plans/readiness.json` v1 contains 100 estimated feature-readiness
points. Credit only verified work packages; in-progress/blocked gets zero. This is a
scope-weighted estimate, not measured engineering hours, code volume or professional
feature parity. Native graph validator alone earns 2; persisted/evaluated graph stays
uncredited. Reweight only with a recorded scope change. Overall baseline 25/100;
managed-storage package can add 3 only after full Android gate. Remaining coordinator
and signing packages are not automatically credited by this work.

POSIX lease caveat: refuse duplicate same-process opens through a canonical-anchor
registry before opening another descriptor. Closing any descriptor for an inode can
release POSIX process locks; relying only on OverlappingFileLockException is unsafe.
Keep registry removal and descriptor release serialized; do not simplify this away.

Publication also requires matching the approved old APK signer, not merely a new
keystore and its own matching pin. Lost old private key remains a real delivery block.
Current readiness remains 25/100: the managed-storage work is implemented and passed
its emulator suite, but this integrated checkpoint is not fully verified. Install-over
gate failed; improved diagnostics could not run after account billing/limit rejection.
Do not report 28/100 or ship an APK until outstanding integration gates are resolved.
