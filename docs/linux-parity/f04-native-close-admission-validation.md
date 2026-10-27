# F04 joined native close admission handoff

The clean frozen source checkpoint is `2dbf9b63e8e771d074556e9f31455a2a5c6da0bf` on `t3/linux-native-host-integration`. It preserves `3025236c`, source `56f1e8d6`, docs checkpoint `cd1aea03` and their entire earlier dependency history. Preparation and timing corrections are separate from each other and from both kernel dependencies:

| Ownership | Source or purpose | Local commit |
| --- | --- | --- |
| Native helper/service | Shared exact side-effect-free preparation | `4a53080e22bc13c9cd71e4ed1ae1c897dae8a07d` |
| Kernel dependency | `df75411f628ff4427c99d61763f7ba428bfe9a48` | `f5382d11306adf86ee56ac3be560fa3e94cac5f2` |
| Native host | Final admission wiring and source composition fixtures | `7fef7cdf9d8be85dab50fadbe7dc501455d58d44` |
| Kernel dependency | `8b803c2c66f3f2d3a68d65725e309de314712dc0` | `2dbf9b63e8e771d074556e9f31455a2a5c6da0bf` |

Both dependency cherry-picks were conflict-free. No earlier a455/2ab/3635 dependency was duplicated. The native lane did not edit hook-owned headers/implementation, Chromium patches, GN, selected manifests, package scripts or JS. F01/F05 remain unjoined. The source checkpoint was reported immediately after the wiring commit and final dependency joined, before final joined checks.

## Native admission boundary

Both synchronous `DeferChromeClose` and posted `ReplyChromeClose` invoke private `AdmitChromeTeardown` for a first non-prepare irreversible boundary. This includes destroy-first only through the same actual pending Chrome certificate/admission helper. The host checks native readiness before calling `CommitLinuxCloseTeardown` and passes a copied weak host/BWI/token/original identity into its native commit callback.

That callback is a nonreentrant atomic native guard followed by `BeginTeardown` only. It rechecks the exact weak browser/window/widget, current committed authorized caller, native token/renderer/close identity, original regular client/scope, successful Seal and clean window-owned ledger. It emits no UI event, reply, I/O, task or event-loop yield. The kernel runs the current Chrome validator before invoking it and checks the same live registration/token/pending state afterward. The host has no remaining direct begin call outside this callback.

Dirty preflight never commits either side. Synchronous retry returns defer true and posts the existing continuation true for a vetoable source rescan; it never falls through as false and never routes retry through ReplyChromeClose for another admission attempt. An asynchronous reply may post before final validation, then resolves retry true without native begin. After committed admission it invokes the already-owned continuation immediately on the same initiating UI task, without another post. Prepare readiness remains posted. Rejection resolves false.

Those are native/registry admission guarantees. The separately discovered forced-source retry defect below still permits destructive Chrome continuation without either commitment and blocks overall close acceptance.

An already begun attempt performs no new renderer authentication/admission. Every path still calls `ReadyForChromeClose` on its retained original lease, including exact weak BWI/window/widget, original token/client/renderer-close identity, Seal and clean window ledger. Renderer replacement cannot adopt that lease or reopen writes. A stale callback or failed retained-ledger check cannot proceed merely because the phase is irreversible. Cancellation still requires confirmed Chrome abort and successful same-FileRunner CancelSeal before producer resumption.

## Missing-certificate correction

The new actual host/registry/controller composition exposed that df754's helper treated an absent first destructive validator as clean. Its unchanged `destroy-no-certificate` regression failed before the final dependency, recording `/tmp/ardia-native-admission-certificate-before.log`. The native lane relayed the exact helper condition rather than implementing a second certificate policy or editing kernel source.

The separate root dependency `8b803c2c` rejects missing first begin/destroy certificates before the native callback. Ordered ignore-unload shutdown supplies its explicit Chrome-owned weak-manager/browser/policy predicate. The same actual host regression passes after joining it. The dependency also bounds retired native token history to 1024 per registration without eviction/re-enabling old tokens; active/idempotent cancellation semantics remain intact.

## Actual source composition coverage

`test_document_close_admission.py` and `document_close_fixture.py` compile the production host/service/canonical helper, actual registry and actual selected patched controller/manager/RFH methods against simulated base, browser, widget, storage and content endpoints. They use the pinned fixture source and current real RFH handler-revision method. They do not substitute a second native admission policy.

All 15 test groups pass under ASan/UBSan, comprising 29 process scenarios. Sixteen variants exercise synchronous/asynchronous final admission after an existing tab gains a handler, an approved handler's revision changes, a new child frame appears or its document changes, with both later approval and veto. Other groups exercise changes during native drain, dirty destroy-first retry and subsequent clean same-identity destruction, missing-certificate refusal, inline asynchronous commitment/first CloseAllTabs without a posted gap, auth/replacement/owner-destruction reentrancy, delayed veto, multiwindow quit cancellation, accepted-write/source loss with exact recovery, retired-owner mixed intent, and retained original leases/ledger checks after begin.

The fixture's async selection drives the actual ReplyChromeClose path; patched source continuations and the real registry determine rescan/commit behavior. Committed-document authorization is additionally exercised by the separate production renderer fixture. This composition uses simulated endpoints and is not a full host or physical renderer/compositor runtime.

The [shared preparation handoff](f04-native-shared-preparation-review.md) records both original preparation findings and before/after evidence. Its 15 parity and 19 storage regressions remain joined, including receipt bounds, actual retirement floors, exact receipt recovery, ticket ordering, count/byte limits and subscription-token invalidation. Public helper signatures and wire/result dictionaries remain unchanged.

## Final joined verification

- `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/ardiacore-linux/tests -p 'test_*.py'`: all 151 tests passed on the frozen joined source, including all 49 kernel/source tests, 15 preparation parity tests, 19 window-storage tests and 15 native admission composition groups.
- The focused actual composition command `python3 -m unittest discover -s engine/ardiacore-linux/tests -p 'test_document_close_admission.py' -v` passed all 15 groups/29 process scenarios with ASan/UBSan. The separately instrumented original host/committed-renderer/utility-close run passed 17 tests.
- All 143 joined renderer tests passed with the existing host import loader. Both Linux/shared browser typechecks, `pnpm --filter @ardia/linux build` and the three production graphs passed. Commands retain Node 26.12.0, pnpm 11.1.3, `--config.verify-deps-before-run=false` and existing frozen worktree-local dependencies.

Logs use `/tmp/ardia-native-final-admission-{all,composition,sanitizers,renderer,linux-types,browser-types,build,graphs}.log`. Native runs use `PYTHONDONTWRITEBYTECODE=1`. No package script or test manifest was edited.

An additional actual-host forced-shutdown probe, `PYTHONDONTWRITEBYTECODE=1 python3 /tmp/ardia-native-admission-forced.py`, passed under ASan/UBSan with a new unprepared window appearing during deferred begin. It reported `forced-new-window native_begin=1 first_destroyed=1 late_vetoable=1 original_seal_retained=1`. Log: `/tmp/ardia-native-final-admission-forced.log`. This checks the clean forced predicate/committed continuation; it does not cover or resolve the failed forced-retry predicate below.

## Known external blocking P1

The fresh kernel review `/tmp/ardia-close-certificate-post-review.md` found that an uncommitted forced-close retry can still reach `BrowserCloseManager::OnLinuxTeardownReady` and call CloseAllTabs. Restarting the same manager or withdrawing ignore-unload while begin waits invalidates the final predicate. The helper correctly returns retry without native/registry begin, but the forced source continuation treats that retry as destructive readiness. The reviewer observed `begun=0 cancellable=1 tabs_closed=1 dispatches=0 destroyed=0 cancels=1`.

The required kernel correction routes retry back through preflight and requires a committed lease before forced destruction. Its owner is active; no coherent followup has been delivered to this checkpoint. The native source and `2dbf9b63` are frozen. No hook-owned edit, WIP copy, polling or wait is performed by this lane. The certificate/token-bound changes passed scoped review, but this P1 and fresh combined host/helper/admission review block F04 acceptance and publication. Passing the currently joined fixtures does not contradict or close the reviewer's two failing forced-retry probes.

## Acceptance limits

The parent reports df754 source independently accepted without P1/P2 findings and certificate/token-bound scoped review accepted; the later forced-retry P1 above remains blocking. A fresh combined native review remains required. No complete Chromium156 host compilation/link or scratch-profile unload/storage/modality/compositor runtime is established by these source tests. The reduced later-thaw fixture's offset qualification does not establish an offset-free full patch series. Direct root CloseNow, unsolicited platform destruction, browser service ownership release, imposed termination and crash/power-loss durability retain the previously documented gaps.

The minimal launch/Mac source choices and public two-pointer close-hook/four-helper ABI remain unchanged. No Chromium155 evidence, engine checkout/output changes, owner profile, app launch, dependency reinstall, publication, push or PR is used. Issues #14/#21/#24 remain open and F04 remains unpublished.
