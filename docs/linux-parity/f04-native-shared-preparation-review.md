# F04 shared commit preparation correction

The later [joined native admission handoff](f04-native-close-admission-validation.md) records the separately committed kernel dependencies and host timing integration. This document preserves the standalone preparation checkpoint and its then-deferred timing boundary.

This corrects the two host/service findings from the combined review of source `56f1e8d6c6bf55efc0d1195cfba77d7ff4eb235e` and its docs-only head `cd1aea0346f1fdab012613240f6f2279836f5fae`. The earlier `3025236c160ef922603fd088413ed81627910bf9` correction and all dependency commits remain preserved. The scope expands only to native Linux document preparation, its service classifier, adjacent fixtures and this handoff. No host close timing, Chromium hooks/patches/GN/manifest, public document protocol, JS, package script, F01 or F05 implementation is changed.

## Reproduction before edits

The full review `/tmp/ardia-native-joined56-review.md` was read before editing. Both supplied probes ran unchanged with ASan/UBSan:

- `PYTHONDONTWRITEBYTECODE=1 python3 /tmp/ardia-joined56-native-review/probe.py`, log `/tmp/ardia-native-preparation-before.log`.
- `PYTHONDONTWRITEBYTECODE=1 python3 /tmp/ardia-joined56-native-review/seam.py`, log `/tmp/ardia-native-preparation-seam-before.log`.

The first probe showed a healthy retired-owner transaction persisting its rename, but the same valid request during read failure recording no ledger failure and allowing close with the name unchanged. Its receipt-size boundary showed a valid 8,388,508-byte envelope returning `ERR_DOCUMENT_LIMIT` on healthy reads, while a failed read recorded a failure that the same limit-refused retry could not resolve. The actual patched-controller seam reported `retired-owner-close failed_accepted_edit=1 ledger_failure=0 watermark=0 actual_kernel_destroyed=1`.

## One preparation path on the actual instance

`LinuxDocuments::CommitAccess` centralizes the existing authorization/subscription, generation-exhaustion and seal checks. Normal Commit invokes it before ReadCurrent, preserving the existing access-before-I/O ordering and errors. After a successful real read, Commit invokes private `const PrepareCommit(client, request)` and persists its prepared candidate before advancing the native envelope, clearing retired-client flags or delivering events.

Preparation reads the actual instance's known envelope, watchers, close seals, exhaustion flag, retired-client set and volatile generation floors. Its only mutations are to local candidate dictionaries. It shares the original exact request/epoch/sequence/duplicate checks, owner-retirement cleanup, checked generation advances, canonical reducer, receipt construction, client-history and revision limits, final envelope validation and exact JSON byte limit. It preserves separate admitted/duplicate/superseded fields. Conflict/nonowner results retain their existing dictionaries, including the optional singleton `reason:'notOwner'`.

Preparation performs no read, write, event, receipt publication, cache advance, watcher change or volatile-floor advance. This holds for both admitted and refused candidates. The method is private and exposes no new renderer/public helper method. Only `LinuxDocumentService` is a friend for classification; public Watch/Commit/Flush/Unwatch/Seal/CancelSeal signatures and wire/result schemas remain unchanged.

`DispatchTracked` now asks that same actual helper instance for preparation admission only after an actual read/data/storage failure. The service no longer owns `KnownCommit`, a guessed last-envelope cache or a second subscriber map. Its observers record actual worker reads/writes without implementing canonical authority or admission rules. A read failure is retained only if that exact native state admits the request through the shared preparation. A limit, malformed request, conflict, nonowner claim, stale request epoch or unauthorized client cannot bypass any preparation check. Failed canonical reads leave the helper's known envelope intact; classification does not adopt corrupt data or changed authority. Admission against that known state protects unresolved intent and grants no write against unread storage.

Preparation admission is not new-write durability proof. Existing recovery still requires the actual successful worker write and matching non-superseded receipt in its exact envelope. Duplicate replies and their default superseded flag cannot create that proof. Existing window-owned exact operation identity, ticket ordering, count/byte bounds, weak-reply independence and subscription invalidation are unchanged. There is no automatic retry, replay, intent discard or operation rewrite.

## Corrected evidence and regression scope

The derived `/tmp/ardia-native-preparation-after.py` preserves the supplied extraction/scenarios, changing the unsafe close expectation to the existing blocking helper for the failed transaction. Its log `/tmp/ardia-native-preparation-after.log` records:

- Healthy control: `retired-owner-control accepted_name=accepted-new-name ledger_failures=0 window_closed=1`.
- Failed read: `retired-owner accepted_name=original-name ledger_failures=1 window_closed=0`.
- Final receipt bound: `encoded-limit envelope_bytes=8388508 healthy_read=ERR_DOCUMENT_LIMIT failed_read_ledger=0 after_retry_ledger=0`.

The derived actual controller/registry composition `/tmp/ardia-native-preparation-seam-after.py` changes only the unsafe retired-owner expectations. It reports `retired-owner-close failed_accepted_edit=1 ledger_failure=1 watermark=0 actual_kernel_destroyed=0`, while the existing delayed veto, multiwindow quit cancellation, accepted-write replacement/exact recovery and retained post-begin lease scenarios remain green. Log: `/tmp/ardia-native-preparation-seam-after.log`.

The new `test_document_preparation.py` compiles actual service/helper/reducer/schema methods under ASan/UBSan. Its 15 cases compare repeated failed-read classification with the later healthy transaction on the same instance and state, checking disk/snapshot/event/write counters before and after each classification. They cover retirement read/write failure, repeated volatile floors, just-below and just-above final receipt capacity, atomic mixed operations, malformed/nonowner/conflict requests, sequence exhaustion/reused identities/duplicates, full client history, epoch/auth/subscription/seal ordering, superseded receipts, generation/revision exhaustion, corrupt/missing/changed-authority reads and write failures. Dry-run refusal must not change a later healthy result.

The window-storage fixture adds two cases to the existing 17 regressions: a valid mixed claim/rename survives two committed replacements and closes only after explicit exact recovery; a receipt-size refusal never becomes a retained failure after a transient read error. These compose actual committed-renderer/host/service/helper methods. The source evidence does not establish complete linked Chromium behavior.

Final preparation verification passed all 127 joined native fixtures, including the 15 new ASan/UBSan preparation parity cases and 19 ASan/UBSan window-storage cases. A separate instrumented host/committed-renderer/utility-close/storage-authority/strict-validation/canonical-helper run passed 20 tests. The joined renderer command passed 143 tests, both Linux/shared browser typechecks passed, and the Linux production build and all three production graphs passed. Logs are `/tmp/ardia-native-preparation-{all,parity,storage,sanitizers,renderer,linux-types,browser-types,build,graphs}.log`. Commands use the existing host import loader, worktree-local dependencies, Node 26.12.0, pnpm 11.1.3 with `--config.verify-deps-before-run=false`, and `PYTHONDONTWRITEBYTECODE=1`. No dependency installation or engine build was needed.

## Deferred kernel/native timing integration

This preparation checkpoint still contains provisional kernel source `3635d8dff4d372f6508fbd614483c02c4f5f9239`, locally `56f1e8d6c6bf55efc0d1195cfba77d7ff4eb235e`. The owner subsequently delivered coherent correction `df75411f628ff4427c99d61763f7ba428bfe9a48`; it is intentionally not joined into this standalone preparation correction. Its dependency join and host admission wiring must remain separate commits. No WIP or uncommitted API is copied here.

The host's current direct BeginTeardown calls in both synchronous DeferChromeClose and asynchronous ReplyChromeClose remain a known unresolved part of the late-handler timing finding. Moving only the registry begun flag cannot fix them. The approved future kernel contract provides `CommitLinuxCloseTeardown` and `LinuxCloseAdmission` plus an internal optional Gate preflight validator. Both host begin paths must consume it after the coherent dependency arrives. Its native callback must atomically recheck the exact weak host/original identity, current authorization, clean ledger and successful seal and perform BeginTeardown only, with no events/replies/storage/task posting/yield. Retry must defer and rescan vetoable preflight without committing native begin; a committed destructive continuation runs on the initiating UI task. The post-begin original sealed lease remains retained across replacement, with no unseal/resume before confirmed Chrome abort and worker cancellation.

That timing join is separate future work. This correction is not F04 acceptance or publication. Fresh independent review of preparation and the eventual combined source, full Chromium156 compilation/runtime, real unload/compositor/modality/storage qualification, exceptional destruction and crash/power-loss guarantees remain open. Issues #14/#21/#24 remain open; no Chromium155 evidence, engine-tree/output changes, owner data, app launch, push or PR is used.
