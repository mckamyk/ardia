# F01 exact-target page-command helper handoff

The implementation lane starts at accepted published baseline `ff59f66eb771ffd7c3c146f4a638943a5e934c3f`, fast-forwarded from `d4df45f7760f81b12f84d42452bfef82412db28e` on tool-managed branch `t3/linux-page-commands`. It implements independent native helpers for [mckamyk/ardia #11](https://github.com/mckamyk/ardia/issues/11), following the complete before-change review at `/tmp/ardia-commands-dialogs-design-review.md`. This is source implementation and a host integration contract. Issue #11 stays open for integration, independent lifetime review and matching Chromium 156 runtime acceptance.

The lane changes no existing host, bootstrap, native BUILD, manifest, dependency, shared browser, demo or macOS file. It introduces no new public page API. `HostWebView` → `HostTab.command()` → `tab.command` already forwards the required methods. The separately committed document and mount-claim authorities must remain authoritative when the host owner joins these helpers.

## Helper API

`ardia/linux/ac_linux_page_commands.h` defines `ardiacore::LinuxPageCommands`. Construct one per committed claimed mount, after resolving its exact native contents through `ClaimedBinding`. All methods and callbacks run on Chromium's UI thread.

The constructor requires:

| Input | Contract |
| --- | --- |
| `contents` | The exact live `WebContents` returned by the current claimed binding; never the Chrome active tab. |
| `service_profile` | The already authorized regular service profile owning this window. Do not resolve another profile or substitute an original profile. |
| `Owner` | A side-effect-free callback returning the originating native browser only while the captured claim/document authority remains valid; otherwise `nullptr`. |
| `Emit` | Deliver the supplied Find dictionary only to the originating authenticated handler and internal tab event ID, with the same authority check. |
| `allow_native_ui` | False in background mode. Derive this from existing native launch configuration, not a new environment read. |
| `Presentation` | An actual installed Linux DevTools presentation/layout callback scoped to this contents and authority. An empty callback deliberately makes DevTools reject as unavailable. The real patched native lifecycle dispatcher must also be installed; presentation alone never enables dispatch. Do not pass a placeholder closure. |

`Handles(command)` recognizes just this lane's five command names. `Dispatch(command, args)` returns `std::nullopt` on dispatched success, or a dictionary containing stable `code` and `message` fields. Pass an error to `done(false, base::Value(std::move(*error)))`; success is `done(true, base::Value())`. The private transport encodes a native empty value as `null`; the public operation remains `Promise<void>`. Find counts never appear in the command reply. Completion acknowledges dispatch, not print-job or file-save completion.

| Command | Exact arguments | Behavior |
| --- | --- | --- |
| `find` | `[text, forward, findNext]` | Calls `StartFinding(text, forward, false, true)`. Validates the `findNext` boolean without mapping it to Chromium's `find_match`. Empty text clears and emits zero counts. |
| `stopFinding` | `[clearSelection]` | Uses `kKeep` or `kClear`; never activates a selected link. Removes observation before stopping. |
| `print` | `[]` | Checks page print policy/restrictions/modal/crash state, then calls exact-contents `printing::StartPrint`; a false dispatch result rejects. |
| `runPageCommand` | `["savePage"]` / `["systemPrint"]` | Preserves save/file-dialog/download/PDF policy and exact-target dispatch. System print checks the selected/PDF renderer and its owning print manager before `StartBasicPrint`. `caretBrowsing` and other strings explicitly reject. |
| `showDevTools` | `[]` / `["toggle"]` / `["inspect"]` / `["console"]` | Policy check and exact-contents open/actions. Toggle sends beforeunload to the exact docked frontend; an existing undocked window is revealed, following Chromium's toggle semantics. |

Errors are `ERR_PAGE_ARGUMENT`, `ERR_PAGE_STALE_TARGET`, `ERR_PAGE_POLICY`, `ERR_PAGE_UNAVAILABLE`, `ERR_PAGE_UNSUPPORTED`, or `ERR_PAGE_DISPATCH`. There is no supported-command success fallback. Disabled printing, print preview/basic-dialog build variants, missing helpers, missing DevTools presentation and background native UI are explicit unavailable outcomes. A different tab's preview, including the global preview-creation flag, never bypasses this target's print restrictions.

## Mandatory dispatcher and authority hooks

Inside the existing `ShellHostView::TabCommand`, resolve `ClaimedBinding(args)` first. For `LinuxPageCommands::Handles(command)`, create/use the binding's current helper and invoke `Dispatch(command, Arguments(args))`, forwarding its structured result as above. Keep unrelated commands in the existing dispatcher. The helper independently verifies exact tab-strip membership, exact BrowserContext/service-profile equality, owning browser profile equality, regular/default-profile restrictions and exclusion of a DevTools browser. These checks do not replace shell authentication.

Construct `Owner` with value captures of the original native tab ID, claim string, renderer generation, `content::WeakDocumentPtr`, weak `WebContents`, weak host and weak handler. Expose a weak-handler accessor in the reserved host file if needed. Never bind a raw `TabBinding`, raw host or raw handler into delayed callbacks.

Every `Owner` invocation must verify all of the following against those captured values:

1. The weak host, handler, document and exact contents remain live. The host is a regular application window, not a utility renderer.
2. The captured document resolves to the shell's committed primary main frame. `OwnsHandler(captured_handler, captured_generation)` and `Trusted(captured_handler->web_ui())` still hold. Do not reactivate a handler from a request or speculative document.
3. `Binding(captured_id)` still points to the captured contents, its claim equals the captured nonempty claim, and its native `renderer_generation` equals the captured generation. Checking today's nonempty claim alone is insufficient.
4. The current binding remains in this exact host/browser and the service-profile restrictions still hold. Return only this original host's browser.

The lookup above validates the old identity; it must never return a replacement binding's owner. The callback must not mutate authority or run application code while validating it.

`Emit` must repeat this original-authority check and call the captured weak handler's `SendEvent("ArdiaWebView", base::NumberToString(captured_id), base::Value(std::move(event)))`. The helper's dictionary is exactly `{type:"findResult",value:{count,active,final}}`. Avoid `ShellHostView::ReportTab` or a fresh owner/handler lookup for these async results: those methods select today's handler. The existing Linux `findResult` adapter reaches the unchanged shared FindBar state.

## Reset and destruction hooks

Store the helper in the claimed binding. `LinuxPageCommands::Reset()` is terminal: it invalidates pending Linux callbacks, revokes the owner callback, removes/stops Find, clears the target and detaches its contents observer, then revokes the retained native DevTools lease after clearing its members. Destroy it and construct a fresh helper for a newly granted claim. `LinuxFind` itself may stop/reopen within a still-valid mount.

Call `Reset()` and destroy the helper **before**:

- `tab.release` clears the claim or disposes a temporary surface.
- A successful reclaim replaces the claim, including the same contents under a new mount.
- `MoveTo` changes windows/owners, and any tab-strip transfer/removal relinquishes ownership.
- `Rebind` replaces contents, including discard/replacement paths.
- `RetireRenderer` clears the committed document, claim or generation.
- Binding, host or parent teardown detaches native viewports.

Reset before a transfer detaches the old tab strip, not after attaching the new owner. Do not reset a predecessor while a speculative renderer or failed reclaim is still uncommitted: existing committed authority must survive cancellation. The helper also resets itself on `WebContentsDestroyed`. The Find observer handles `OnFindTabHelperDestroyed`, removing itself before the helper vanishes, and removes observation before stopping a query. A stale result fails the captured owner check and stops/unobserves. Chromium owns the query/session filtering, including stale replies after query replacement or stop/reopen.

## DevTools presentation gap and exact layout seam

The reserved host's current `LayoutOverlay()` fills `BrowserView` with the Linux shell and hides `contents_web_view()`. Calling `OpenDevToolsWindow` alone does not establish visible, usable DevTools. This lane has not implemented or exercised native presentation. Leave `Presentation` empty and expose a structured unavailable result until the host owner supplies a real layout integration.

Pinned 156 exposes `BrowserView::GetContentsContainerViewFor(exact_contents)` and that container's `devtools_web_view()`. Chrome's `MaybeUpdateDevtools(exact_contents)` updates its own container; that method is private and cannot be called from this helper. The integrator must coordinate Chrome's container with Linux's per-pane viewport. Merely making a child visible beneath the full shell overlay is insufficient.

For Linux-owned docking, use `DevToolsWindow::GetInTabWebContents(exact_contents, &strategy)` after readiness, then `ApplyDevToolsContentsResizingStrategy(strategy, pane_bounds, &devtools_bounds, &page_bounds)`. Place the exact frontend in a Linux-owned `views::WebView` with input/focus/occlusion ownership above the shell at `devtools_bounds`, and constrain that pane's page viewport to `page_bounds`. Coordinate detachment from Chrome's `GetContentsContainerViewFor(contents)->devtools_web_view()` before hosting the same frontend elsewhere; Chrome can reattach it during its own updates, so a permanent owner/layout hook is required. Respect `strategy.hide_inspected_contents()` and shared overlays. Do not change Chrome's selected tab as a way to select a pane.

The patched native dispatcher calls `Presentation` through an epoch-and-lease-checked `SetLoadCompletedCallback` after validating its original authority. Native Show and posted activation have their own earlier guards; see the lifecycle correction below. The host also needs updates on viewport/bounds changes, DevTools docking/strategy changes, frontend destruction and close/veto. Those callbacks must carry the same original authority. Restore the page rectangle and focus after actual close; a beforeunload veto must retain the frontend. `GetDevToolsWebContents()` provides the exact frontend even during initialization; `GetInTabWebContents()` returns only a docked frontend ready for presentation.

For an undocked-only first implementation, retain Chrome's existing undocked browser/widget ownership and validate the exact tools' `IsDocked()` state. The normal undocked show path in pinned `DevToolsWindow::Show` creates/shows its own browser and focuses that frontend. `SetIsDocked` and `CloseWindow` are private. Do not call them, use a browser-based toggle, mutate profile-wide dock preferences, inject frontend scripts, or substitute a new tab target to force undocking. A Linux-scoped engine creation/docking seam retaining the exact inspected contents would be required to guarantee undocked presentation independently of saved frontend settings. Until docking or that seam exists and is exercised, no visible DevTools functionality is claimed.

Print preview/file-dialog positioning and native modal keyboard ownership likewise need real-host acceptance with the overlay; policy-preserving dispatch is not visual proof.

## GN registration for the host owner

Add these sources to the existing Linux-only `//ardia/linux:linux` target:

- `ac_linux_find.cc`, `ac_linux_find.h`
- `ac_linux_page_commands.cc`, `ac_linux_page_commands.h`

Relevant direct dependency labels verified in pinned 156 BUILD files are `//components/find_in_page`, `//chrome/browser/devtools`, `//chrome/browser/devtools:devtools_toggle_action`, `//chrome/browser/ui/tab_contents`, `//components/policy/core/common:common_constants`, `//components/web_modal`, `//pdf:buildflags`, and `//printing/buildflags`, alongside existing base/content/profile/tab-strip/browser/common/prefs dependencies. Include `//chrome/browser:browser_process` for `g_browser_process` where required by GN's header checking. With `enable_printing`, add `//chrome/browser/printing`; its implementation is supplied through Chrome's existing printing graph. With `enable_pdf`, add `//chrome/browser/pdf`, `//components/pdf/browser`, and `//pdf:features`. Linux docking integration would additionally use `//chrome/browser/devtools:devtools_contents_resizing_strategy` and the corresponding Views container dependencies. Respect the existing GN feature imports and do not add platform targets outside `is_linux`.

These labels and header ownership were inspected in the pin; no combined-host GN generation, header check or real 156 compilation has run. GN integration belongs to the host owner and must be verified there. No new shared engine service route is needed.

## Pinned API evidence

Small primary-source excerpts were fetched read-only from Chromium tag `156.0.8078.47` into `/tmp/ardia-f01-156`. No Chromium checkout or output was modified. Relevant references are the [Find helper implementation](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/components/find_in_page/find_tab_helper.cc), [Find observer](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/components/find_in_page/find_result_observer.h), [print implementation](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/chrome/browser/printing/print_view_manager_common.cc), [Chrome command policies](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/chrome/browser/ui/browser_commands.cc), [DevTools public/private API](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/chrome/browser/devtools/devtools_window.h), [DevTools lifecycle](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/chrome/browser/devtools/devtools_window.cc), [BrowserView API](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/chrome/browser/ui/views/frame/browser_view.h), and [contents container API](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/chrome/browser/ui/views/frame/contents_container_view.h). The unchanged Mac `ardiacore_tab_commands.mm` was read for policy/targeting parity. Local 155 source is not qualification for this implementation.

## Verification and remaining acceptance

Worktree-local frozen dependencies were installed with pnpm 11.1.3 under both `/tmp/ac-pod.lockf` and `/tmp/ac-pods.rw`; no manifest or lockfile changed. pnpm checks used `--config.verify-deps-before-run=false`; Python used `PYTHONDONTWRITEBYTECODE=1`.

- Before implementation: all 94 existing Linux host tests and four committed-renderer fixtures passed.
- `python3 -m unittest discover -s engine/ardiacore-linux/tests -p test_page_commands.py -v`: 12 passed, including ASan/UBSan runs of the actual two production helper translation units. Cases cover exact-target two-pane routing, search arguments/results/session changes/previous/next/empty/hide, stale authority, release/reclaim/transfer/rebind/destruction, reentrant self-destruction, print/save policy, PDF/selected renderer preflight, explicit unsupported/errors, DevTools modes/toggle/stale load and background UI refusal. Disabled printing/preview/basic-dialog/PDF combinations compile in the fixture.
- Final nearby native suite: all 46 tests passed. It includes the 12 new cases and existing committed-document/renderer/claim/disposal tests; it does not integrate the new helper into the real host.
- `node --experimental-strip-types --import ./apps/linux/scripts/host-test-loader.mjs --test apps/linux/src/hostPageCommands.test.mjs`: three passed using production `HostTab` and bootstrap transport. Covers exact envelopes, distinct result events, replacement/disposal and structured rejection without retry.
- `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`: seven passed.
- Existing `pnpm --filter @ardia/linux test:host`: 94 passed after implementation. Add `src/hostPageCommands.test.mjs` to that test script when the manifest owner integrates the lane.
- `pnpm --filter @ardia/linux typecheck`: passed.
- `pnpm --filter @ardia/linux build`: passed, 1169 modules.

Logs are `/tmp/ardia-f01-{deps,native-before,host-before,native-after,native-final,js,host-after,typecheck,build}.log`. The C++ fixtures substitute Chromium/base types and native UI, and model Chromium Find session filtering. They compile actual lane production methods but do not prove Chromium ABI, real header compatibility, dispatcher/reset wiring, dialog behavior or compositor input. The JS fixture simulates native replies and cannot prove the helper is registered. Background tests use fake native entry points or deny UI; no visible test window or owner profile was opened.

The required independent after-change review must cover captured authority, helper/observer destruction and callback reentrancy, then the joined host hooks and GN graph. Matching fullhost156 remains unbuilt. Keep #11 open until real split panes produce correct Find counts/navigation/dismissal, actual print output and saved page/PDF files, plus visibly usable Show/Toggle/Inspect/Console with correct pane, focus, overlays and native modal ownership. Record current source/UI/binary identities during that acceptance. No push or PR occurred in this lane.


## P1 lifecycle correction after independent review

The immutable implementation commit `670755fdf1077359c2929dd5a31d817f7ee7df54` was reviewed in `/tmp/ardia-f01-post-review.md`. The supplied `/tmp/ardia-f01-review-670755fd/probe_devtools.py` reproduced the finding **before edits**: pinned `LoadCompleted()` calls native `Show()` before its completion callback, so helper Reset could leave a native Show/focus/posted activation alive while suppressing Linux presentation. The ASan/UBSan reproduction log is `/tmp/ardia-f01-review-reproduced.log`. A completion-only fixture was insufficient.

The correction introduces `ac_linux_devtools_authority.{h,cc}` and the independent Linux-only `engine/patches/chromium-linux-devtools-authority.patch`. The patch is intentionally unselected until the manifest owner joins it. No existing selected manifest, GN, host or bootstrap file was changed. Find, Print, Save and their argument contracts are preserved.

### Lease, native instance and registration

`LinuxDevToolsLease` captures the exact weak contents and the same immutable originating claim/document/generation validator used by the page helper. Revocation is terminal and idempotent. Chromium's instance retains the revoked lease as a tombstone; loss of the helper cannot turn that instance into an unrestricted Chrome operation. The native revoke callback holds only a weak DevToolsWindow plus its exact epoch. The helper retains one current lease, revoking a superseded lease after a successful native takeover; native epoch checks keep that revocation from touching the newer operation. Failed dispatch restores the predecessor helper lease.

`LinuxDevToolsState` stores the current epoch/lease and whether the frontend was created under Ardia authority. A new explicit Ardia operation transfers current Ardia frontend ownership to its lease. A deliberate new Chrome command relinquishes Ardia frontend ownership and advances the epoch. An already-existing Chrome frontend never becomes Ardia-owned merely because an Ardia action uses it. Old revocation and posted callbacks compare the exact epoch and lease before any effect. Normal unhosted instances have no managed state and retain their default lifecycle.

`ScopedLinuxDevToolsOperation` hands the lease into the exact native command synchronously. Its capture is consumed once, with a separate one-use creation scope for the constructor. A nested unrelated Chrome operation cannot inherit the caller's ambient lease. Constructor reentrancy and an existing-instance takeover are checked again before scheduling or installing the Linux ready callback. These scopes carry raw contents only for synchronous pointer equality; no raw host/binding/frontend is captured asynchronously.

The only enabling interface is an actual `LinuxDevToolsDispatcher` function pointer. It accepts exact contents, `LinuxDevToolsAction`, a shared lease and a `base::OnceClosure` ready callback. The patched `DevToolsWindow::RegisterLinuxDevToolsLifecycle()` installs the real implementation. Until that method is called from Linux host startup, the registry is empty and `LinuxPageCommands::DevTools` returns `ERR_PAGE_UNAVAILABLE` before native opening. This is a callable native seam, not a readiness flag. A real Linux layout/presentation callback is independently required; registering the lifecycle hook alone does not supply layout.

### Native guard and cancellation points

The new patch touches exactly three pinned files: `chrome/browser/devtools/devtools_window.h`, `devtools_window.cc` and `devtools_window_views.cc`. Every change is behind `BUILDFLAG(IS_LINUX)`.

- The constructor captures the lease before installing frontend delegates or running creation callbacks. The exact-content Toggle entry binds each new explicit operation before scheduling native UI. After creation or a reentrant native update, the originating ticket must still own the instance.
- `ScheduleShow` and `Show` reject revoked authority. `LoadCompleted` checks authority **before** native Show, then uses weak-instance/ticket checks before touching state or running ready callbacks afterward. Show rechecks after native callbacks before further show/focus effects.
- Docked Show posts activation with the captured ticket. The posted closure checks instance lifetime, exact epoch and original lease **before** calling `ActivateInspectedTab` or resolving a browser. `ActivateInspectedTab`, `ActivateWindow`, `ActivateContents`, undocked browser creation and native browser update/lookup paths have independent authority guards.
- `GetInTabWebContents` refuses a quarantined Ardia-owned frontend. It preserves a pre-existing Chrome frontend that Ardia never owned.
- Revocation cancels the owned pending action/ready callback, then quarantines only the current epoch's Ardia-created frontend: hide its exact Aura native view, and its own undocked native window if present. The `aura::Window::Hide()` and `ui::BaseWindow::Hide()` signatures were checked in pinned 156. The public RenderWidgetHostView interface does not supply Hide and is not used for quarantine.
- Reset never starts Close, dispatches beforeunload, destroys edits, or closes another owner's frontend. Quarantined contents remain owned by Chrome's existing native instance until deliberate takeover or normal inspected-contents destruction. A fresh valid operation may reveal/adopt that same frontend with edits intact.
- Initial `SetIsDocked` and native load bookkeeping still complete under a revoked lease. Only outward presentation is blocked; a new owner can adopt the ready instance without hanging on a dropped initial docking handshake. Loaded revoked instances cannot redock or create a new native window.

Explicit docked toggle still enters Chrome's actual Close/BeforeUnloadFired flow. The state keeps one in-flight close ticket and at most one newest retry ticket. A new owner can supersede the pending close. If it also requests Close, the old beforeunload result is vetoed and the new request is posted with its own ticket; the old result cannot destroy the new frontend or cancel that legitimate new close. A current user's veto preserves the frontend and restores its ready stage without retrying the vetoed request. Undocked toggle preserves Chrome's reveal behavior. Browser/page beforeunload interception with no managed frontend close retains the existing path.

The helper clears all of its members before invoking native lease revocation. Native quarantine and ready callbacks tolerate frontend/helper destruction and ownership changes during callbacks. There are no helper/native strong-reference cycles: the instance owns a lease, whose validator and revocation callbacks reference weak objects. Retention is bounded to one helper lease, one native current ticket, one pending close and one retry per exact inspected contents, plus already-posted tasks until they drain. The existing `InspectedContentsClosing` path remains unchanged and disposes the native frontend when its exact inspected contents ends.

### Exact parent integration changes

The manifest/GN/host owner must perform these changes together before enabling DevTools:

1. Select `chromium-linux-devtools-authority.patch` for Linux only after the Linux layer is materialized. It adds no shared/Mac hook.
2. Add a small `//ardia/linux:devtools_authority` source set containing `ac_linux_devtools_authority.cc` and `.h`, with only `//base` and `//content/public/browser` dependencies. Do not put the authority implementation in the existing host source set.
3. Add `//ardia/linux:devtools_authority` to the Linux dependencies of `//chrome/browser/devtools:devtools`, and to `//ardia/linux:linux`. The former already supplies Chrome DevTools; the new low-level target must not depend on DevTools or the host, avoiding a dependency cycle. Ensure the DevTools target has `//ui/aura` for the Linux native-view hide call. The header is `ardia/linux/ac_linux_devtools_authority.h`.
4. Call the patched public `DevToolsWindow::RegisterLinuxDevToolsLifecycle()` once during Linux host startup, before accepting shell page commands. Do not install a placeholder callback or bypass the registry check in the page helper.
5. Retain all earlier committed claim/document, per-binding Reset and exact-profile hooks. The host's layout teardown must detach its old per-pane DevTools view before retiring ownership. Actual docking/undocked visibility and native modal input still require the separate layout integration described above.

### Correction verification and limits

The new lifecycle test reads only the three small pristine pinned source files from `ARDIA_F01_PINNED_SOURCE` (default `/tmp/ardia-f01-156`), verifies their SHA-256 identities for Chromium `156.0.8078.47`, copies them into a temporary directory and applies the actual new patch with `patch --fuzz=0`. All three files apply with zero offset and zero fuzz. The tests compare the non-Linux preprocessor projection with the pristine sources, with equal tokens for every touched file. The existing selected patches have no overlapping hunks in these three files. This is focused patch coverage, not materialization/checking of the entire selected Chromium/Helium series.

Eighteen ASan/UBSan lifecycle cases compile the actual patched `RegisterLinuxDevToolsLifecycle`, lease binding/revocation, exact native Toggle, ScheduleShow, Show, LoadCompleted, OnLoadCompleted, InspectedContentsClosing, SetLoadCompletedCallback, ActivateInspectedTab, ActivateWindow, ActivateContents, SetIsDocked, Close, CloseWindow, BeforeUnloadFired and GetInTabWebContents bodies alongside the actual page helpers and low-level authority implementation. Native view/browser/delegate/renderer dependencies and constructor machinery are stand-ins. The test exercises actual Show-before-callback order and actual posted-task guard scopes, not only a fake completion callback. It covers load-after-reset in both initial docking/load orders and successful later adoption, posted activation after reset/move, Chrome/Ardia takeover, pre-existing Chrome frontend preservation, undocked quarantine/reveal, stale close plus a new Ardia/Chrome close, veto, one-use scopes, callback destruction/reentrancy, default unhosted behavior and missing real dispatcher.

Commands and results:

- `ARDIA_F01_PINNED_SOURCE=/tmp/ardia-f01-156 PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/ardiacore-linux/tests -v`: 64 passed, retaining the original 46 and adding 18 lifecycle cases.
- The focused 18-case lifecycle suite passed again after the final native Aura API correction. The frozen pin hashes and zero-fuzz/non-Linux projection checks pass in that run.
- The three production HostTab/bootstrap transport tests and seven bootstrap tests passed unchanged.
- Linux UI sources/manifests are unchanged. The earlier typecheck and 1169-module UI build evidence at `670755fd` remains the UI verification; neither is presented as fullhost156 compilation.

Logs are `/tmp/ardia-f01-correction-{page,lifecycle,lifecycle-final,native-final,transport,bootstrap}.log`. The standalone before-change probe remains available under `/tmp/ardia-f01-review-670755fd`; it describes the old bug and was not rewritten to hide the reproduction. The newly committed source suite is the after-change probe. It skips the 18 lifecycle tests when the small pinned sources are unavailable and never downloads a source tree automatically.

A new independent lifetime review is required for this lease/state/patch and, separately, its eventual host/GN/manifest join. No real Chromium cache, output or owner profile was touched. No native browser, visible dialog or application window was launched. Matching fullhost156 compilation/runtime, actual print/save output and visible DevTools layout remain unverified. Issue #11 stays open. No push or PR occurred.
