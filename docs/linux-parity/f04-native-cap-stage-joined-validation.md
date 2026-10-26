# F04 native cap and requesting-stage source checkpoint

This source checkpoint preserves the independently accepted shared canonical preparation `4a53080e22bc13c9cd71e4ed1ae1c897dae8a07d` and preceding ledger/watch corrections. It follows frozen source `2dbf9b63` and docs `d5e96d7d` without rewriting their history.

## Separate commits and dependency mapping

| Role | Source commit | Local commit |
| --- | --- | --- |
| Own renderer-first cap correction | own | `caaf0c828a9876f8d155d7166ebe22c388ec1c11` |
| Kernel forced retry correction | `26c602e3a913d0fd3471077504190112d3ad7196` | `25108bc8837d51b6eb83924c461549aba9ca94ba` |
| Kernel requesting-stage correction | `1ad74032a5019a417d31c140b77d840220f731d9` | `54a5d3e8dd4c826d664ef1ba67a32a3557fcdacb` |
| Own stage plumbing and actual-host composition | own | `7eff33448e7136e004b08d23cfb10ff9030c87c9` |

Both dependency cherry-picks applied without conflicts. Git range-diff reports both mapped commits identical to their source commits. No previous kernel dependency was duplicated. Hook-owned header, registry, Chromium patch and selection metadata change only through those exact dependency commits. No GN, package, JS, F01, F05 or PageState edits are included in the owned corrections.

The parent supplied scoped independent source acceptance of both kernel commits in `/tmp/ardia-close-force-stage-accepted.md`. That acceptance does not cover this owned native integration; fresh combined review is required.

## Native integration

`AdmitChromeTeardown(identity, token, stage)` passes the unchanged requesting stage to `CommitLinuxCloseTeardown(browser, token, stage, native_commit)`. Both synchronous DeferChromeClose and posted ReplyChromeClose supply their actual stage. There is no default stage, old overload, pending-stage inference or host retry-flag mutation. Kernel admission selects the exact active pending stage and current Chrome certificate.

The native closure remains only a weak host/browser and exact original identity/token/authentication/Seal/clean-window-ledger recheck followed by atomic BeginTeardown. It performs no event, reply, storage operation, task posting or event-loop yield. Retry does not call native Begin; synchronous retry retains async callback ownership and posts the existing true continuation, while the already posted asynchronous reply resumes inline. Committed asynchronous continuation has no additional posted gap. Already-begun destruction still requires the retained original native lease and clean window ledger.

The separate cap correction binds before HandOff and BrowserWindow Close, preserving the global handed-off-without-token refusal and actual registry cancellation proof. See [cap evidence](f04-native-close-cap-review.md).

## Focused source verification

`test_document_close_stage.py` compiles actual host/service methods, the production registry and selected patched Chromium controller/manager/RFH methods under ASan/UBSan. Four groups covering 14 process scenarios pass (`/tmp/ardia-native-stage-composition.log`). They exercise Begin/Destroy overlap in both request orders while Seal is pending and while native readiness is posted, current handler veto and approval, forced same-manager restart, ignore-unload policy withdrawal, and forced concurrent Destroy. Dirty preparation remains sealed, cancellable and non-destructive until the actual handler decision. Approval commits and finishes the same original native identity.

The cap checkpoint separately passed 12 ASan/UBSan cap/ordering groups and the original before/after reproduction. Its worker cancellation failure case proves there is no premature producer-resume event. Final joined native, renderer, type, build and graph results are recorded below.

## Final joined evidence

All final commands run against frozen SOURCE `7eff33448e7136e004b08d23cfb10ff9030c87c9`. This later evidence checkpoint changes only this document. Worktree-local frozen dependencies are reused with Node 26.12.0, pnpm 11.1.3 and `--config.verify-deps-before-run=false`; Python uses `PYTHONDONTWRITEBYTECODE=1`. No install or Chromium build is performed.

The supplied frozen overlap reproduction also fails before the correction: `/tmp/ardia-native-stage-frozen-before.log` reports one handler dispatch but phase Cancelled, writes resumed and token empty before any handler acknowledgment. The joined stage composition retains the same sealed identity/token until actual veto or approval. The cap before/after logs and production extraction limits are recorded in the separate cap document.

| Check | Result | Log |
| --- | --- | --- |
| Full joined native fixtures | 178 passed in 203.909 seconds | `/tmp/ardia-native-cap-stage-final-all.log` |
| Additional actual host/committed-authority/utility-close ASan+UBSan | 17 passed | `/tmp/ardia-native-cap-stage-final-sanitizers.log` |
| Default host files plus presentation, document, completion, transport and close lifecycle renderer suites | 143 passed | `/tmp/ardia-native-cap-stage-final-renderer.log` |
| Nearby platform/branding tooling | 19 passed | `/tmp/ardia-native-cap-stage-final-platform.log` |
| Linux TypeScript | Passed | `/tmp/ardia-native-cap-stage-final-linux-types.log` |
| Shared browser TypeScript | Passed | `/tmp/ardia-native-cap-stage-final-browser-types.log` |
| Linux production build | Passed | `/tmp/ardia-native-cap-stage-final-build.log` |
| Production module, input and persistence graph checks | 3 passed | `/tmp/ardia-native-cap-stage-final-graphs.log` |

Native command: `python3 -m unittest discover -s engine/ardiacore-linux/tests -p 'test_*.py' -v`. The additional sanitizer harness `/tmp/ardia-native-followup-sanitizers.py` instruments `test_document_host`, `test_document_renderer` and `test_utility_close`. The new cap, stage and admission compositions already compile with ASan/UBSan in the default native suite.

Type/build commands are `pnpm --filter @ardia/linux typecheck`, `pnpm --filter @ardia/browser typecheck` and `pnpm --filter @ardia/linux build`, invoked through pnpm 11.1.3 with the flag above. Graph command: `node --test apps/linux/scripts/module-graph.test.mjs apps/linux/scripts/input-module-graph.test.mjs apps/linux/src/livePersistenceGraph.test.mjs`.

The renderer command runs from apps/linux with `node --experimental-strip-types --import ./scripts/host-test-loader.mjs --test`, all ten unchanged default host manifest paths, plus these package-integrator-owned additions:

- `src/hostTabPresentation.test.mjs`
- `src/liveDocuments.test.mjs`
- `src/livePersistenceBinding.test.mjs`
- `src/livePersistenceGraph.test.mjs`
- `src/liveCompletionPersistence.test.mjs`
- `src/hostDocumentTransport.test.mjs`
- `src/windowCloseLifecycle.test.mjs`

The package manifest is unchanged in this lane. The joined renderer command includes the existing default suites; no redundant second default run is claimed. Git diff checks pass, helper/service files match shared-preparation `4a53080e` exactly, and the two kernel mappings pass range-diff comparison.

## Qualification limits

These extracted actual-source fixtures use simulated browser/base/storage endpoints. Full Chromium156/native-header compilation, linking and runtime remain unverified; no Chromium155 build evidence substitutes for them. Complete later-thaw offset qualification, physical storage/unload/compositor/modality and exceptional synchronous destruction, process crash or power loss remain open. Accepted worker persistence survives renderer reply loss; no guarantee is claimed for unaccepted renderer work bypassed by exceptional force destruction.

The pinned source route classification remains:

| Route | Qualification at this checkpoint |
| --- | --- |
| Ordinary button/compositor/keyboard close and application quit | Manager/controller preparation, final current-document/handler preflight, Begin before ClosePage/CloseAllTabs and Destroy before deletion are joined. Ordinary unload rescans and posted SynchronouslyDestroyBrowser occur on these routes and require the same retained lease. Actual native compositor/keyboard/runtime routing is unverified. |
| Ordered end-session/silent-exit ignore-unload branch | The accepted force dependency now requires the exact initiating committed lease before destruction; dirty policy retries return to preflight. Imposed termination deadlines cannot guarantee renderer-only intent. |
| Direct root Widget::CloseNow, unsolicited platform destruction or browser service ownership release | These can bypass initiating manager/controller preparation. Normal downstream platform CloseNow after approved close inherits its existing seal; direct unprepared destruction cannot establish a drain afterward. A version-correct future guard must coordinate root destruction before teardown and remaining browser ownership before BrowserManagerService::Shutdown releases entries, preserving synchronous caller contracts without blocking UI-dependent continuations. |

Accepted FileRunner operations retain independent service ownership on the existing BLOCK_SHUTDOWN sequence through renderer/reply loss and orderly shutdown. This preserves accepted persistence, not unsubmitted JS state or crash/power-loss guarantees. No new window or renderer may adopt an old lease.

No browser/profile/cache/build-tree/publication action is performed. F04 is unpublished; issues #14/#21/#24 remain open. Fresh combined native integration review is required at the frozen joined source SHA.
