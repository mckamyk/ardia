# F04 native review correction handoff

The later frozen-44059262 recheck found additional ledger and subscription defects. The [window persistence ledger correction](f04-native-ledger-review-corrections.md) supersedes this document's filename-based recovery and pending-watch qualification and records the subsequent coherent Chromium dependency join. That dependency remains provisional pending independent and combined review.

The review of `db8cc756acaa384fdcf45d8275e94a4a69338b04` requested three corrections. All three were reproduced before source edits with `PYTHONDONTWRITEBYTECODE=1 python3 /tmp/ardia-f04-review-probes.py`. The exact before outputs are retained in `/tmp/ardia-f04-native-review-before.log`:

- Timeout reopened native writes while the pinned Chromium 156 rescan scheduled deletion with dirty renderer intent.
- An accepted old-renderer session write failed after replacement, its native ledger was empty, and replacement close succeeded.
- A retired renderer's posted browser close produced a fresh request/event in its replacement.

The probes compile actual extracted native glue and the pinned 156 rescan/closing branch under ASan/UBSan with fixture dependencies. They are source reproductions, not a complete Chromium build or runtime.

## Native ledger and posted close

Storage pending/failure state belongs to the native window and is no longer cleared by renderer retirement. Every accepted completion updates that ledger while the native window survives, independently of renderer reply eligibility. Only a successful retry of the same storage name removes its failure; unrelated writes cannot clear it. Old replies still pass through their existing weak/current-document guards.

The posted native close now captures weak host and browser references plus exact window, renderer and close identity. Immediately before BrowserWindow Close it checks the same browser/window ownership, current authorized renderer, exact sealed identity, clean storage ledger and live widget. A stale task returns without invoking Widget Close or requesting a new generation.

Ten focused native host modes passed, including both regressions. All ten passed ASan/UBSan; the committed-renderer/document lifetime fixture also passed under ASan/UBSan. Logs are `/tmp/ardia-f04-native-p2.log`, `native-p2-sanitizers.log` and `native-p2-activation.log`.

## Coordinated close repair

The host consumes the agreed `/tmp/ardia-linux-close-hook-contract.md`. `StartLinuxHost` registers the two callbacks. The first authenticated empty-token defer synchronously locates/creates its native request and binds a fresh UnguessableToken before returning. Preparation continuations are bounded and asynchronous, independent of the renderer acknowledgment awaiting WidgetClosing. A ready same-lease revalidation returns false without invoking its callback.

Native reject/deadline uses `CancelLinuxCloseAttempt`. Merely invoking Chrome's discard callback is not proof; false returns ERR_WINDOW_CLOSE_IN_PROGRESS and retains the seal/write gate. Confirmed Chrome abort starts same-FileRunner CancelSeal with the originally authenticated client. Only successful worker cancellation emits `ArdiaShell`/`shell` `{name:'shell.closeCancelled',data:{windowId,rendererGeneration,closeGeneration}}` and completes rejection successfully. JS deduplicates the event and successful rejection by exact opaque identity, resumes at most once, retains frozen producers after transport failure, and ignores stale identities. It also suppresses a late drain acknowledgment after cancellation.

The initial posted native close marks a handoff before calling Chromium, so a deadline cannot unseal merely because a close callback has not arrived. Successful begin/destroy boundaries are irreversible. Before begin, readiness requires the current authenticated committed document, exact seal and clean native-window storage ledger. After begin, the retained original native lease can finish the same attempt after renderer loss: exact weak BWI/widget, window ID, token, original client/scope and renderer-close identity remain fixed. Retirement retains this authority and does not CancelSeal. No new generation is adopted; all document/watch/session/generic writes remain window-blocked. Replacement watch requests held before confirmed cancellation are bounded and released only after unseal, with fresh generation/authorization checks.

The narrow read-only `ArdiaShell.windowIdentity` case takes exactly args:[] and returns `{windowId,rendererGeneration}` from the actual committed authorized regular-profile caller. It rejects inactive, private, malformed and zero-generation queries, mints no document client, and uses the existing request-generation-bound reply guard. It does not publish a bootstrap counter. No F05 controller/helper wiring is included.

Focused source fixtures cover Chrome preparation vs WidgetClosing completion, refusal without actual abort proof, cancellation ordering through worker completion/event delivery, deadline/lost-ack retention after begin, stale native tokens, prepared callback survival, renderer replacement before begin, and retained original leases afterward. The pinned Chromium 156 late-rescan replay now reports `native_can_close=1 native_blocks_writes=1 chromium_delete_scheduled=1 renderer_dirty=0`; renderer commit is rejected with ERR_DOCUMENT_CLOSING. Its artifact is `/tmp/ardia-f04-native-cancel-after.cc` and log `/tmp/ardia-f04-native-cancel-after.log`. These remain extracted source fixtures with simulated dependencies, not a full engine runtime.

## Joined correction ownership

The independent ledger/posted-task correction is `ee36af19b4739b430ce317b137b81581e8e02c0f`. The coordinated native/JS cancellation and identity-query correction is `8ca8616518e9db4e4d589177a120baca7a55e0ea`. Both follow reviewed source `db8cc756acaa384fdcf45d8275e94a4a69338b04`; earlier own and dependency commits are preserved.

The separate Chromium agent owns `ac_linux_close_hooks.{h,cc}`, selected patch/manifest and Linux BUILD target wiring. This lane did not edit those files. Its exact coherent dependencies were cherry-picked without conflicts:

| Original dependency | Local dependency |
| --- | --- |
| `a45525b9a6534d06dc5190130cd94ff5e5af560d` | `2f0311385bf78e34d8df11efe66c5b7534dccc30` |
| `2ab024bbb98fabddfb9f766b5f0b32c6aad1c6a2` | `3af9b776f49eab2213d1434813e84442499abf80` |

The committed-document activation/authorization and dedicated temporary disposal methods remain unchanged. `RetireRenderer` retains `if (!RetireDocuments()) return;` before advancing generation. The public document API and structured helper results are unchanged. No F05 controller or helper dependency is included.

## Current close-route qualification

The selected Linux-only `chromium-linux-document-close.patch` targets pinned Chromium `156.0.8078.47`. The fixture applies the existing hosted-window patch and this patch with zero fuzz, verifies pinned source hashes and unchanged macOS series, and compiles actual patched manager/controller methods plus the actual base-only hook registry. Native host fixtures separately compile actual host methods with simulated browser/widget/task dependencies. These are source and interface checks, not a linked host or real compositor run.

| Route | Joined source gate and remaining qualification |
| --- | --- |
| Ordinary compositor/window button/keyboard BrowserWindow Close | `HandleBeforeClose` and `OnWindowClosing` prepare the authenticated attempt; the existing Widget gate remains. Runtime platform routing is unverified. |
| Ordinary application quit, including empty windows/download decisions | `TryToCloseBrowsers` prepares before shutdown beforeunload, and `CloseBrowsers` rechecks before Window Close. Exact weak quit observers revoke manager/download continuations on confirmed cancellation. Page beforeunload behavior is retained; shared F21 warnings remain separate. |
| Unload rescan directly reaching tab close or deletion | `ProcessPendingTabs` gates before destructive ClosePage; `OnWindowClosing` gates begin before CloseAllTabs; `OnWindowCloseComplete` gates destroy before deletion scheduling and the posted SynchronouslyDestroyBrowser task rechecks destroy. These paths occur during ordinary close/quit, not only exceptional shutdown. |
| Ordered ignore-unload end-session/silent-exit branch | `CloseBrowsers` now gates prepare/begin/destroy before CloseAllTabs and SynchronouslyDestroyBrowser. It retains Chromium's existing policy once the same lease succeeds. Imposed OS deadlines and abrupt termination cannot guarantee renderer-only intent. |
| Direct root `views::Widget::CloseNow`, unexpected platform destruction, browser service ownership release | These can bypass the initiating manager/controller path; no complete barrier guarantee is claimed. Normal downstream platform CloseNow after approved Widget Close inherits its seal. An unsolicited destruction cannot be vetoed afterward. Remaining ownership-release paths require version-correct upstream coordination before native teardown/service Shutdown, preserving callers' synchronous destruction contracts; adding a late observer is insufficient. |

Already accepted FileRunner work owns the service independently of renderer replies and continues on its BLOCK_SHUTDOWN sequence during orderly process shutdown. Successful canonical writes retain that persistence guarantee through source-renderer loss. Direct/abrupt destruction before JS drain does not guarantee renderer-only intent; BLOCK_SHUTDOWN does not establish power-loss durability. No callback may adopt a new browser or replacement renderer to complete an old lease.

Materialization requires the Linux-selected patch series and the existing Linux layer copy, which now includes `ac_linux_close_hooks.{h,cc}`. Chromium lifetime/unload targets depend only on `//ardia/linux:close_hooks`; that target depends only on base, avoiding a host link cycle. The host Linux target consumes it. The minimal launch target and macOS source/series choices are preserved. No Chromium 155 tree/output was changed.

The pinned 156 review supersedes the earlier 155 preflight inference: the reproduced destructive continuation is an ordinary unload-phase rescan; 156 excludes shutdown-preflight state from that direct branch. Full 156 compile/runtime, independent joined native review, exceptional teardown policy, modality and power-loss qualification remain open. Issues #14/#21/#24 remain open; no push or PR is performed.

## Joined verification and package seam

After both hook dependencies joined, `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/ardiacore-linux/tests -p 'test_*.py'` passed all 76 native fixtures. That includes 23 close-hook tests: 19 ASan/UBSan execution scenarios compiling actual patched pinned-156 methods and actual registry, and four source/materialization/GN-selection checks. A separate instrumented run of `test_document_host`, `test_document_renderer` and `test_utility_close` passed all 17 tests with ASan/UBSan, compiling actual host/glue methods. Logs are `/tmp/ardia-f04-native-joined.log` and `/tmp/ardia-f04-native-review-sanitizers.log`.

The joined default host plus owned/helper/presentation/close renderer fixtures passed 142 tests under the existing host import loader. The unchanged default `pnpm --filter @ardia/linux test:host` passed 90 tests. Linux and shared browser typechecks, `pnpm --filter @ardia/linux build`, and all three production graph tests passed. Commands used Node 26.12.0, pnpm 11.1.3 and `--config.verify-deps-before-run=false` with existing worktree-local dependencies; no dependency reinstall or engine build was needed. Logs are `/tmp/ardia-f04-native-review-{renderer,default,linux-types,browser-types,build,graphs}.log`.

The package integrator must add `src/windowCloseLifecycle.test.mjs` alongside the previously handed-off `src/hostTabPresentation.test.mjs`, `src/liveDocuments.test.mjs`, `src/livePersistenceBinding.test.mjs`, `src/livePersistenceGraph.test.mjs`, `src/liveCompletionPersistence.test.mjs` and `src/hostDocumentTransport.test.mjs`, preserving `--import ./scripts/host-test-loader.mjs`. This lane did not edit package.json. The new close suite covers timeout retention until exact cancellation, event/reject deduplication, cancellation before quiesce/during drain, and stale-event exclusion across attempts.

Native source review must assess the complete joined host plus real hook registry/Chromium patch. Separate fixture success does not prove their full-engine ABI, GN graph, compositor routing, beforeunload interactions or process shutdown behavior. No matching 156 host compilation, visible app, owner profile, publication or issue closure was performed.
