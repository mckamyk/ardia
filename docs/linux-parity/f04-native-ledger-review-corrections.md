# F04 window persistence ledger correction

The later combined review found divergent cached commit classification. The [shared preparation correction](f04-native-shared-preparation-review.md) supersedes this document's KnownCommit/cache-based admission description and records the remaining kernel/native timing dependency.

This corrects the three independently reproduced host findings on frozen `440592623436f792cdc366e112dc40a59a41074d`. Its implementation is preserved exactly in `3025236c160ef922603fd088413ed81627910bf9`. It preserves that complete history and its dependency separation. The native correction does not edit the close-hook header/implementation, Chromium patch, GN targets, selected manifest, canonical helper/reducer/schema, public document protocol, or F05 helpers. The separate owner's coherent Chromium correction is now joined as described below; it remains provisional pending independent and combined review. Issues #14/#21/#24 remain open.

## Before and after evidence

Before any source edit, the complete supplied review and probe were read, and the original probe ran unchanged under ASan/UBSan:

`PYTHONDONTWRITEBYTECODE=1 python3 /tmp/ardia-host-44059262-recheck.py`

The log `/tmp/ardia-host-native-followup-before.log` records all three failures:

| Reproduction | Frozen source |
| --- | --- |
| Failed new generic intent followed by successful stale same-file snapshot | `disk=old-snapshot ledger_failures=0 window_closed=1` |
| Canonical write fails after accepted dispatch and renderer replacement | `failed_original_receipt=0 ledger_failures=0 pending=0 window_closed=1` |
| Watch queued during cancellation, then explicitly unwatched | `explicit_unwatch=1 pending_watches=0 final_watched=1` |

The derived `/tmp/ardia-host-44059262-recheck-after.py` retains the same production extraction and three scenarios. Its expected close continuations now assert refusal at prepare, begin and destroy, rather than asserting the unsafe successful close. It logs to `/tmp/ardia-host-native-followup-after.log`:

| Reproduction | Corrected source |
| --- | --- |
| Stale same-file success | `disk=old-snapshot ledger_failures=1 window_closed=0` |
| Lost canonical failure | `failed_original_receipt=0 ledger_failures=1 pending=0 window_closed=0` |
| Explicit unwatch | `explicit_unwatch=1 pending_watches=0 final_watched=0` |

These are actual extracted production host, committed-renderer, service and canonical-helper methods with simulated task/browser/widget/storage dependencies. They are source evidence, not a complete Chromium host build or desktop runtime.

## Window-owned operation tickets

The native window retains exact operation identities and monotonically increasing native ticket sequences across committed renderer replacement. Every accepted completion updates its window ledger before invoking the weak renderer reply. Source replacement or handler destruction affects reply delivery, not the accepted worker operation or its native outcome.

Generic operation identity includes native filename, write versus remove and the entire exact text. Session identity includes the entire base and proposed session; the merged result does not identify the operation. A success recovers only the same operation's prior failed ticket. Multiple failed payloads for the same filename remain distinct. An earlier success cannot clear a later failure, even if its UI completion is delayed. An explicit matching successful retry can recover across replacement in the same native window.

Admission caps are checked before posting accepted work: at most 128 pending calls, 128 retained distinct identities, 32 MiB of encoded retained identity and 32 MiB of encoded pending work. Repeated pending submissions reserve their own byte budget even when sharing an identity. One operation is bounded to 16 MiB plus 1 KiB, accommodating both bounded session inputs. These are encoded-data budgets plus bounded object overhead, not an exact heap measurement. Native sequence exhaustion rejects new acceptance. Capacity rejection preserves failures; matching retries can reuse retained identities and release capacity after successful completion. No payload or recovery identity is logged or included in error/event diagnostics.

No automatic retry, document restoration, operation rewrite, producer claim, or new public recovery endpoint is introduced. Unrecoverable work keeps the close barrier blocked. The original post-begin sealed native lease and write gates are unchanged.

## Native-only canonical outcome seam

The approved `LinuxDocumentService::DispatchTracked` returns the unchanged `LinuxDocumentResult` alongside private native durability metadata. All instrumentation and canonical methods run on the same existing FileRunner. The service cannot be moved or copied because its read/write observers belong to its own sequenced lifetime.

An actual failed canonical transaction write records a persistence failure after the canonical helper has validated and reduced that transaction. A missing/unreadable, corrupt, regressed or unexpectedly changed canonical authority is classified against the last valid native envelope and current authenticated subscription. Failed reads do not replace that cached authority. The existing canonical operation validator and reducer check bounded grammar, epoch, native receipt sequence and operation applicability; malformed requests, invalid identity, known conflicts/nonowner claims, and nonstorage limit refusals do not become failed durable intent. This preflight classifies read failures only; it does not authorize a write or change the helper's result. Canonical helper results and optional `reason:'notOwner'` pass through unchanged.

A recovery proof requires a successful actual canonical write during that tracked worker dispatch, a non-superseded helper result, and the receipt in that exact written envelope matching the native client request ID, epoch and full operation list. A snapshot watermark or `status:'committed'` alone provides no recovery proof. A duplicate dispatch has no new transaction write, so its default `superseded:false` cannot manufacture proof. Superseded transactions and their later duplicates conservatively retain previous failures.

Canonical ticket identity includes the natively resolved storage authority, document, epoch and complete operation list, preserving all incarnation/generation/field preconditions and list ordering. Only request ID and observed cursor are excluded. A new authenticated client in the same native window may explicitly submit identical operations through the existing commit API. Only its actual worker transaction proof can recover eligible older failures in that window. Another window's success cannot clear them. Changed producer ownership/fences or conflicting preconditions may prevent recovery; the host leaves those failures blocked rather than weakening operations.

## Subscription cancellation

Each valid watch/unwatch advances a native subscription identity and supersedes pending watch callbacks asynchronously. A queued watch carries both the committed renderer generation and subscription identity. Cancellation replay rechecks both after the native cancellation event, including reentrant unwatch during event delivery. Superseded callbacks receive a structured subscription error. An unwatch prevents queued replay and later changed events; a subsequent watch gets its own fresh identity and is the only replay allowed.

## Validation and remaining integration

The new `test_window_storage.py` compiles actual host/renderer/service/session/canonical methods under ASan/UBSan. Its cases cover the three findings, exact generic/session/canonical recovery, actual worker ordering and delayed completion, source/reply loss, same-authority other-window isolation, malformed/read/limit/nonowner classification, superseded/duplicate receipts, count/byte exhaustion including duplicate pending work, unwatch/re-watch/event reentrancy, native owner destruction and all write gates after irreversible begin.

Final source verification passed all 93 native fixtures with `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/ardiacore-linux/tests -p 'test_*.py'`. This includes all 17 new ASan/UBSan storage regressions. A separate ASan/UBSan-instrumented run of the original host/committed-renderer/utility-close fixtures passed another 17 tests. The unchanged default host file list plus the helper/presentation/document-transport/window-close suites passed all 143 renderer tests under the existing import loader, including the canonical production persistence graph. Linux and shared browser typechecks and the Linux production build passed using Node 26.12.0 and pnpm 11.1.3 with `--config.verify-deps-before-run=false`; existing worktree dependencies needed no reinstall. Logs are `/tmp/ardia-native-followup-{all,storage,sanitizers,renderer,types,browser-types,build}.log`. The explicit before/after probe logs above also passed under ASan/UBSan.

The required native integration seam is solely `DispatchTracked` and its native outcome type; public LinuxDocuments methods, wire methods and reply dictionaries remain unchanged. No package script or JS implementation change is needed for this correction. Existing renderer/close transport suites remain applicable.

## Corrected Chromium dependency join

The source owner delivered `3635d8dff4d372f6508fbd614483c02c4f5f9239` directly atop its preserved a455/2ab chain. Only that new correction was cherry-picked onto clean native `3025236c`, producing clean frozen joined checkpoint `56f1e8d6c6bf55efc0d1195cfba77d7ff4eb235e`, immediately reported for review before checks ran. There were no conflicts, copied WIP or integration edits. The native host/service/ticket/subscription code is byte-for-byte unchanged from `3025236c`; committed renderer authority, temporary disposal, public document APIs and the post-begin original sealed lease are preserved. F01/F05 remain unjoined.

| Source dependency | Local separate dependency commit |
| --- | --- |
| `a45525b9a6534d06dc5190130cd94ff5e5af560d` | `2f0311385bf78e34d8df11efe66c5b7534dccc30` |
| `2ab024bbb98fabddfb9f766b5f0b32c6aad1c6a2` | `3af9b776f49eab2213d1434813e84442499abf80` |
| `3635d8dff4d372f6508fbd614483c02c4f5f9239` | `56f1e8d6c6bf55efc0d1195cfba77d7ff4eb235e` |

The new Chromium-owned internals carry immutable per-dispatch beforeunload completion authority, outermost cancellation coordination, strong quit-manager participation, begin gating immediately before destructive work, and event-driven retry wakes. The public two-pointer hook ABI and four registration/bind/cancel helpers remain unchanged; the host needs no new integration calls. This records the supplied source correction, not independent acceptance of it.

Joined verification ran the native suite once: `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/ardiacore-linux/tests -p 'test_*.py'`, passing all 110 tests, including the 40 corrected kernel/source fixtures and all 17 ASan/UBSan window-storage fixtures. A separate instrumented source host/committed-renderer/utility-close run passed 17 tests. The existing-import-loader renderer command passed 143 tests; both Linux/shared browser typechecks, `pnpm --filter @ardia/linux build`, and all three production graphs passed. The graph command was `node --test apps/linux/scripts/module-graph.test.mjs apps/linux/scripts/input-module-graph.test.mjs apps/linux/src/livePersistenceGraph.test.mjs`. Logs are `/tmp/ardia-native-kernel-joined-{native,sanitizers,renderer,linux-types,browser-types,build,graphs}.log`. Node/pnpm invocation and frozen worktree dependencies match the ledger validation above. No package script/manifest was edited.

The two native storage-authority and strict-validation fixtures were additionally instrumented with ASan/UBSan and passed: actual registry sequencing/lifetime, read error distinction, reserved bootstrap/API protection, rejected operations and generation-limit authority preservation. Command: `PYTHONDONTWRITEBYTECODE=1 python3 /tmp/ardia-native-kernel-joined-authority.py`; log `/tmp/ardia-native-kernel-joined-authority.log`.

An additional temporary composition probe, `PYTHONDONTWRITEBYTECODE=1 python3 /tmp/ardia-native-joined-registry.py`, compiles the actual corrected `ac_linux_close_hooks.cc` with extracted production host/service/canonical methods under ASan/UBSan. All six modes passed: `proof`, `ledger`, `stale-write`, `watch`, `lease`, `lifetime`. It verifies that invoking a discard closure without confirmed abort does not unseal; actual registry cancellation precedes worker unseal; canonical and stale same-file failures block all stages until exact recovery; unwatch invalidates queued replacement watch; the same original lease remains irreversible across renderer replacement; and registry/host destruction suppress stale UI completion safely. Log: `/tmp/ardia-native-kernel-joined-composite.log`. The first compile required correcting a class/struct declaration in its temporary fixture shim; repository source was unchanged.

The composition uses simulated base/task/browser/widget/controller endpoints and current-handler authorization, not the full Chromium host. Committed-document authorization is exercised by the separate source renderer fixture, while patched pinned-156 controller/RFH methods run in the corrected kernel fixtures. These checks do not establish their complete linked-engine behavior or real desktop routing. No host implementation or hook-owned source edit was needed for this join.

A fresh combined native/kernel review, full Chromium156 compilation and runtime qualification remain required. Direct destruction, imposed termination, F21 warnings and crash/power-loss guarantees remain open. No Chromium155 evidence is used; no engine checkout/output, owner profile, launch, publication or issue closure is performed.
