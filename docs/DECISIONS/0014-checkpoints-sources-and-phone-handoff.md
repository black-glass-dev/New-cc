# ADR 0014 — Diagnostic checkpoints, derived sources and user-owned phone testing

2026-10-23. Implemented/host-tested; Android compilation and acceptance pending.

## Storage and failure semantics

Reuse `AtomicFileStore` for same-directory temporary write, close, file sync, rename
and parent-directory sync. Native checkpoint ordering is receipt preflight → current
recovery → prepared receipt → primary replacement/reparse → committed receipt → durable
recovery removal → clean receipt. Six logical boundary hooks exercise interruptions;
they are not a physical power-cut or fsync fault-injection harness. A late failure can
leave a valid durable primary with no recovery. Saved revision advances only on full
native success; managed catalog/acknowledgement remain separate fallible operations.

The bounded `OWNCUTCK` v1 receipt holds generation/revision/phase/project ID. It is
**not** a checksummed WAL, durable history, asset transaction, automatic promotion rule
or power-loss proof. Project/autosave schema remains v1. Malformed known-format receipt
preflight may retain a fresh supported draft and then fail; unsupported future receipt
or I/O errors fail before recovery is touched. Recognized future `OWNCUTPJ` recovery
must never be overwritten or cleared. Clearing recovery also validates the receipt.

Explicit confirmed repair may archive **only typed malformed receipts**, and requires
a readable primary plus the managed writer lease/fingerprint guard. Directory, nonregular,
I/O and unsupported-version errors are refused; no recovery or source media is deleted.
Native operations serialize with edits. Managed save failure reconciles the primary
fingerprint only when its loaded model exactly matches the current engine model;
external changes remain conflicts. There is no catalog/native multi-file transaction.

## Identity, media and JNI

Kotlin supplies random UUID project IDs rather than process-local counters. Sources is
a deterministic projection of referenced media, usage and required source range, with
20-entry UI pages and earliest-associated-clip selection. It does not preserve unused
imports, establish asset UUIDs, scan offline media, or eliminate full JSON/UI allocation.

Convert Java UTF-16 and standard UTF-8 explicitly, including exception messages; reject
unpaired surrogates, malformed encodings and NUL inputs. The original host JVM fixture
uses production JNI symbols with `-Xcheck:jni`, including supplementary characters and
large paged snapshots. This validates neither Kotlin compilation nor ART/device behavior.

## Faster development handoff

Per user instruction, phone tests belong to the user, not an automatic emulator wait.
Default CI keeps native regression, resources, Android build/lint/test-source compilation,
APK alignment and approved-signer/package/version checks. Emulator/render/install-over
are manual `run_device_tests=true`; requested failures block candidate publication.
Signed candidates disclose `deviceValidation=not-run` when skipped and never assert
phone success. This supersedes earlier mandatory-emulator candidate-publication wording
in ADR 0012/0013, not product acceptance criteria or legacy signature compatibility.
No unsigned/random-signer update or uninstall/data-clear workaround is authorized.

## Remaining work

Android build/phone acceptance; controller/task lifecycle extraction; bounded UI snapshot
consumption; durable asset identities/offline diagnostics; real persisted/evaluated nodes;
advanced color and a specified `.ccraw` camera/RAW pipeline. Weighted roadmap credit stays
25/75 until acceptance evidence supports changing its ledger. No new APK from this batch.
