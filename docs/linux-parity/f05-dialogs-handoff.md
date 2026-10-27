# F05 Linux dialogs: helper handoff

This lane implements source helpers for mckamyk/ardia #15. It does not establish full acceptance of #15 or #31. The tool-managed branch `t3/linux-dialogs-parity` was clean at `d4df45f7760f81b12f84d42452bfef82412db28e` and fast-forwarded to accepted helper-only F04 base `d6ed050d5aa57ddb7855d227e21af674062cfbdb`. That base already includes the baseline through published layer 56 and the canonical Live persistence seam. No dependencies were cherry-picked again and no native host glue was inherited from the separate integration branch.

The lane reads the complete before-change review at `/tmp/ardia-commands-dialogs-design-review.md`. It changes no shared browser implementation, macOS/demo source, host dispatcher, bootstrap, hostModules, GN, Vite or package manifests. Source fixtures do not close issues.

## Renderer adapter

`apps/linux/src/hostDialogs.ts` supplies `createHostDialogs(host)` and the lazy `linuxHostDialogs()` singleton. `shims/shell.tsx` explicitly exports its confirm/prompt and registers those same functions with the existing Expo native-module provider. Both public package functions and direct Expo module calls reach the adapter. Public signatures stay unchanged.

The adapter obtains identity asynchronously before each begin using `hostModuleMethod("ArdiaShell", "windowIdentity", host)()`, sending exactly `{args:[]}` and receiving `{windowId,rendererGeneration}`. The returned windowId must equal host.bootstrap.windowId and the generation must be a positive, canonical decimal string of at most 20 digits. The listener is installed before the query. Each pending entry captures its own queried generation; there is no cached identity promise or generation shared across requests/retirement. Query failure, unsupported backend, malformed identity or a mismatched window explicitly rejects the public promise and removes its pending registry entry, without sending begin or falling back to a browser dialog.

This replaces the bootstrap-constant requirement in preserved source commit `693aedc3612ec92ad3c22366616a551d6c2879fa`. Native feasibility inspection established that ShellDataSource snapshots Bootstrap before ReadAsset, while committed renderer authority activates only in PrimaryPageChanged. A module-import-time generation constant therefore cannot safely represent actual committed authority. Do not predict a counter, publish zero, manufacture a speculative identity, or add a bootstrap field for this handshake. The native owner separately implements windowIdentity with captured current committed handler/document, owning window/profile and default-regular-profile authentication. The query must reject requests lacking that authority and take no JS identity arguments. Unsupported windowIdentity remains an explicit integration gap until that native implementation is joined.

The adapter registers one shell listener before querying identity or invoking begin and keeps at most 16 requests, including identity queries still awaiting replies. IDs combine cryptographic UUID entropy and a monotonically increasing adapter-local suffix; even a repeated injected entropy value cannot reuse a completed request ID. IDs are restricted to 16–96 ASCII letters, digits, colon, underscore or hyphen. Generation values are never converted to JS numbers.

Transport is exactly:

| Direction | Operation | Payload/result |
| --- | --- | --- |
| invoke | `ArdiaShell.windowIdentity` | `{args:[]}` → `{windowId,rendererGeneration}` from committed native authority |
| invoke | `ArdiaShell.beginDialog` | `{args:[{dialogId,kind:'confirm'|'prompt',options}]}` |
| begin reply | successful registration | `{accepted:true}` immediately |
| invoke | `ArdiaShell.cancelDialog` | `{args:[dialogId]}` |
| event | module `ArdiaShell`, channel `shell` | `{name:'shell.dialogResolved',data:{dialogId,windowId,rendererGeneration,result}}` |

Results received before identity capture or begin dispatch are ignored. Results received after begin dispatch but before its acknowledgement are buffered, then delivered only after successful acknowledgement. Failure or a bad acknowledgement rejects and attempts idempotent native cancellation. Results are accepted only for the current window, captured native generation and registered ID; duplicates and malformed results are ignored. Prompt text is trimmed and empty text becomes null. Confirm Cancel preserves the checkbox value only if this request supplied suppression. No settings preference is stored natively.

After begin acknowledgement the public promise has no decision timer and the native begin transport request is finished. The unmodified 30-second bootstrap request timer therefore does not bound user deliberation. An identity-query or begin transport timeout is an error, never confirmation. Callers may retry with a new ID.

`retire()` removes listeners, safely cancels all pending public promises and attempts native cancellation only for requests whose begin was sent. It is attached to the existing `onBeforeClose`, which bootstrap calls on final pagehide, not when a close warning starts. Late identity success/failure after retirement cannot send begin or affect a replacement adapter. A second registry/liveness guard after identity validation and immediately before direct begin dispatch also prevents begin when retirement occurs between those steps. The options snapshot is retained from the original public call. If native authority changes after identity was queried, native begin must reject the originating stale handler/document; the adapter rejects that failure and discards any buffered result. Wrong-generation results are ignored. Native renderer retirement must independently cancel its queue; JS delivery is not required for native cleanup. Do not invoke adapter retirement on a provisional close request or a close transport timeout.

## Native API and authentication hooks

All constructors, controller operations, presentation, completion and destruction run on `content::BrowserThread::UI`; production entry points assert that sequence.

`LinuxWindowDialogs(window_id, profile_identity, actual_requesting_widget, background)` in `ac_linux_dialog_views.{h,cc}` owns the production controller. It observes the actual parent Widget with both destruction notifications. A close callback alone cannot cover parent CloseNow. A weak parent Widget is captured by the presenter. Background mode cancels safely without creating or showing a dialog. Supply the host's existing background-instance authority, not a renderer argument.

`LinuxDialogController(window_id, profile_identity, presenter)` in `ac_linux_dialogs.{h,cc}` can instead receive an injected presenter for tests. Its public operations are:

| API | Required hook |
| --- | --- |
| `ReplaceRenderer(const std::string& generation) -> bool` | On an authenticated new committed shell document, before accepting windowIdentity/begin requests. Same active generation is idempotent. The immediately retired generation and malformed values are rejected. Generations are otherwise opaque equality tokens; native committed-document authority owns issuance and freshness. Old registry entries and presentations are canceled before the new generation is published. |
| `Begin(const LinuxDialogClient&, const base::DictValue& request, LinuxDialogCompletion, std::string* error) -> bool` | After request authentication and exactly one dictionary arg validation; true means registered. Reply `{accepted:true}` immediately, never retain the begin transport callback as the user-decision callback. |
| `Cancel(const LinuxDialogClient&, const std::string& dialog_id) -> bool` | Exactly one string arg, authenticated same renderer. Absent already-completed IDs are idempotent. Wrong renderer/window/profile is rejected. |
| `RetireRenderer(const std::string& generation)` | On handler/document retirement, renderer crash/replacement or final shell teardown, before allowing another renderer to attach. Stale retire operations cannot cancel a newer renderer. |
| `ParentDestroyed()` | Before/at actual native parent destruction; production Views owner supplies this hook independently of host close callbacks. |
| `GetWeakPtr()` | Capture this for delayed controller actions; never capture a raw controller or host. |

`LinuxDialogClient` contains `window_id`, `profile_identity`, `renderer_generation`, `authenticated_shell` and `default_regular_profile`. These fields are native authorization evidence, not a deserialization target. Build them from the current committed shell handler and document only after the host's `Trusted`/`OwnsHandler` checks, including current primary frame/document, matching real BrowserContext/Profile and existing default-regular-profile restriction. Use the loaded owning service profile's native identity consistently. Ordinary web pages, foreign utility windows, retired handlers/documents and wrong profiles must fail before Begin/Cancel. Never look up a different active host/window as a fallback.

The controller rechecks the evidence against its window/profile/current generation. Omitted options.windowId means this authenticated requester; explicit mismatch or invalid type rejects. Native options validation bounds titles to 4096 bytes, messages and prompt values to 16384, button labels to 1024, suppression and placeholder to 4096. There are at most 16 pending dialogs per window; each window serializes by registration sequence.

The Begin completion callback is `base::OnceCallback<void(base::DictValue)>`, receiving the complete shell result envelope. Capture the originating handler's weak identity, weak committed document and native generation in this callback. Deliver via `Event("ArdiaShell", "shell", base::Value(std::move(envelope)))` only if that exact origin is still authenticated. Do not forward it through today's handler after a replacement. Final renderer retirement may suppress event delivery while still closing all dialogs. A callback may reenter or destroy the controller; no code after it dereferences a raw controller. The registry entry and active surface are removed before the moved OnceCallback runs.

The real Views presenter uses client-owned `DialogDelegate` and Widget, `MessageBoxView`, window modality and the authenticated Widget's native parent. `MakeCloseSynchronous` returns Enter/accept, Cancel/Escape/frame-close decisions; text and checkbox are read before destroying the Widget. Normal Cancel preserves suppression. External dialog or parent CloseNow and renderer/controller destruction force cancellation with suppressed false. Controller completion from UI actions is posted through a moved OnceCallback, avoiding raw presenter/host captures and deletion during dialog event dispatch. Every delayed controller response carries the original registration sequence, so cancellation/replacement followed by an old callback cannot complete a newer request.

## Exact source and GN hooks for the host owner

Add these sources to the existing Linux-only `//ardia/linux:linux` source_set:

- `ac_linux_dialogs.h`
- `ac_linux_dialogs.cc`
- `ac_linux_dialog_views.h`
- `ac_linux_dialog_views.cc`

The controller needs `//base` and `//content/public/browser`. The Views presenter additionally needs `//ui/views`, `//ui/base`, `//ui/base/mojom`, `//ui/color` and the dialog-button generated header owner `//ui/base/mojom:ui_base_types`. Pinned 156 `ui/base/mojom/BUILD.gn` places `ui_base_types.mojom` in target `mojom` and `dialog_button.mojom` in target `ui_base_types`; both dependencies are intentional. Existing Linux GN already has several of these dependencies; the integrator must resolve exact generated-header target visibility with the pinned 156 GN graph and add direct dependencies as required. This lane has not edited or generated the full native graph.

Include `ac_linux_dialog_views.h` in the host, keep one LinuxWindowDialogs owner per authenticated native window, create it with that actual Widget, and call ReplaceRenderer only after native committed-document authentication. Retire its old generation on every host renderer-retirement path and clear handler/document authority before destroying the dialog owner. Add windowIdentity, beginDialog and cancelDialog to native dispatch/capabilities without changing shared public methods. windowIdentity must accept exactly zero args, authenticate the actual current committed caller and return its own `{windowId,rendererGeneration}` without installing or predicting a renderer. Native begin must independently revalidate current committed ownership after the separate query; the query is identification evidence, not a reservation or transferable authorization. Result events use the existing module/channel envelope. No engine service route, shared engine ABI or React Native Alert callsite is needed.

Do not revert committed-document authority, mount-claim checks, canonical documents or F04 close barriers while wiring these hooks. The small shell.tsx change intentionally leaves its existing close barrier text intact.

## F21 returning decision proposal for the close-hooks lane

This is a proposed shared seam, not an applied shared source change. It extracts the warning portion already in `state/windows/windowClose.ts` and returns a decision; it does not clone that file into Linux. The shared Mac caller keeps its existing small-window branch and invokes closeWindowAnimated only after the decision returns allow.

```ts
export async function decideWindowClose(windowId: string): Promise<'allow' | 'cancel'> {
  const s = useBrowser.getState();
  if (!s.windows[windowId]) return 'cancel';
  const count = closingTabs(windowId).length;
  if (s.settings.warnBeforeClosingWindow && count >= MIN_TABS) {
    const { confirmed, suppressed } = await confirm({ ...closeWindowDialog(windowId), suppression: 'Don’t ask me again', windowId });
    if (suppressed) useBrowser.getState().updateSettings({ warnBeforeClosingWindow: false });
    if (!confirmed) return 'cancel';
  }
  return 'allow';
}
```

The close-hooks lane must account for its existing small-window behavior at its caller, and revalidate the live window and same native close generation after the await. This seam must not call closeWindowAnimated, remove tabs/windows or acknowledge native closure. A dialog error or cancel must invoke `rejectWindowClose` with that same `{windowId,rendererGeneration,closeGeneration}`. Repeated close attempts must share the same pending decision. Generation types and the F04 request/acknowledge/reject envelope remain unchanged.

Required ordering is native close request → warning decision and suppression setting mutation → canonical final quiesce/drain/Seal → normal native close → Chromium beforeunload/DevTools veto outcome. Suppression must enter the canonical session save before Seal, including checkbox changes on Cancel. A normal native close acknowledgement is not proof of actual closure. Preserve all tabs/store records until native closure is committed; a veto must leave them present.

The existing F04 native 30-second close deadline is a concrete integration blocker. The close-hooks lane reported it can unseal while Chromium continues closing. User deliberation cannot be represented by an outstanding 30-second begin or close transport request. Before entering the warning, the native close protocol needs an explicit decision-pending/deferred state bound to the same native close generation, with its close deadline paused or not yet started. Start any bounded persistence/commit deadline only after allow and the canonical barrier transition. Expiry must never confirm, force-close, destroy a pending dialog, or implicitly resume producers/unseal while native closure may still continue. A transport rejection/timeout provides no authenticated native veto acknowledgement. The agreed native cancellation event is `ArdiaShell` channel `shell`, `{name:"shell.closeCancelled",data:{windowId,rendererGeneration,closeGeneration}}`, carrying the same opaque decimal request identity. Emit it only after Chromium confirms abort and CancelSeal succeeds; generation mismatch, transport success/failure or timeout is not cancellation. The close-hooks owner must wait for this authenticated cancellation/veto coordination before resuming producers; dialog code cannot repair this by timing out its UI. Renderer death and parent destruction remain forced cancellations with no destructive acceptance.

After the seam is applied by that owner, test Cancel → same-generation reject, repeated request deduplication, checkbox persistence before barrier, save failure/retry, delayed deliberation beyond 30 seconds, beforeunload/DevTools veto, and positive close without early store removal. Keep shell dialog result/cancel dispatch usable while the close coordinator is pending. This lane's real shared warning consumer test proves the current guard honors Cancel and updates suppression without tab removal; it does not claim that the current F04/F21 native join satisfies these requirements.

## Verification and remaining evidence

Worktree-local dependencies were installed frozen with pnpm 11.1.3 and ignored lifecycle scripts while holding both existing `/tmp/ac-pod.lockf` and `/tmp/ac-pods.rw` flocks. No manifest or lockfile changed. Native fixture commands use `PYTHONDONTWRITEBYTECODE=1`.

Focused commands:

```bash
node --experimental-strip-types --import ./apps/linux/scripts/dialog-test-loader.mjs --test apps/linux/src/hostDialogs.test.mjs apps/linux/src/hostDialogConsumers.test.mjs apps/linux/src/hostDialogModule.test.mjs
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/ardiacore-linux/tests -p test_dialogs.py -v
npm exec --yes --package=pnpm@11.1.3 -- pnpm --config.verify-deps-before-run=false --filter @ardia/linux typecheck
npm exec --yes --package=pnpm@11.1.3 -- pnpm --config.verify-deps-before-run=false --filter @ardia/linux build
```

The corrected JS checks pass 22 tests: async identity ordering, malformed/mismatched identity and unsupported backend, repeated query-failure cleanup, stale query replies, retirement during/after identity await, fresh query per begin, native authority rejection between query and begin, identity-pending queue bounds, buffered early results, failed or malformed begin/ack, real unmodified bootstrap timer cleanup/pagehide, duplicates, window/generation isolation, prompt trim/null, checkbox Cancel, destructive labels, bounded queues, repeated entropy, retirement/retry, and a two-minute simulated pending decision with no hung native begin request. Real shared consumers cover essential rename, independent ordinary-row inline rename, tile-folder rename, canceled bookmark-folder creation, profile-delete Cancel/Confirm and unavailable-identity rejection without deletion, and deduplicated window-warning Cancel with suppression mutation. The module check executes actual shell.tsx exports/provider registration, hostDialogs and Expo/package wrappers, with unrelated shell dependencies stubbed.

Linux typecheck and the production UI build pass. The original helper validation and the identity-correction rerun of the existing Linux host suite pass 136 tests; the full native fixture suite passes 38 tests, including these two programs; the unmodified bootstrap suite passes seven tests. The shared conventions check fails only on three pre-existing environment reads in `ac_input_frame_rate_linux.cc` and `pip/ac_pip_hyprland.cc`, verified present at the accepted d6ed050d base; neither file was changed. Original helper logs are `/tmp/ardia-f05-{build,host-tests,native-tests,bootstrap-tests,dialog-tests,conventions}.log`. Correction logs are `/tmp/ardia-f05-identity-{typecheck,build,host-tests,dialog-tests}.log`; native sources/signatures and event envelopes are unchanged in the separate correction commit, so native fixture evidence above belongs to the original helper commit and its focused final reruns.

The two native programs compile and execute the actual controller and Views presenter sources with minimal base/Views fixture headers and ASan/UBSan. They exercise FIFO serialization, simultaneous window independence, queue bounds, malformed options, wrong profile/window/ordinary-page/retired authority flags, default owner, prompt values/placeholder/selection, Enter/Cancel decisions, direct CloseNow, parent CloseNow, retired/stale responses, synchronous presenters, failure, atomic replacement before publication, reentrant callbacks and synchronous/asynchronous completion destroying its controller. The presenter callback is copied before invocation so its bound state outlives a completion that destroys the controller. They are source/lifetime fixtures; their fake authorization flags do not prove real host route authentication, and fake Views widgets do not prove real desktop rendering or keyboard focus.

Read-only inspection used official Chromium `156.0.8078.47` headers: [DialogDelegate](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/ui/views/window/dialog_delegate.h), [Widget](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/ui/views/widget/widget.h), [WidgetDelegate](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/ui/views/widget/widget_delegate.h), [WidgetObserver](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/ui/views/widget/widget_observer.h), [MessageBoxView](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/ui/views/controls/message_box_view.h), [Textfield](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/ui/views/controls/textfield/textfield.h), [MdTextButton](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/ui/views/controls/button/md_text_button.h), and [string_util](https://raw.githubusercontent.com/chromium/chromium/156.0.8078.47/base/strings/string_util.h). These confirm the selected method/ownership shapes. The DialogDelegate unique_ptr overload requires a restricted owned-by-widget passkey; this implementation uses client-owned Widget and delegate plus MakeCloseSynchronous instead. Header copies are scratch evidence under `/tmp/ardia-dialogs-156-headers`, not a source download or a matching native build.

Full native Chromium 156 is unbuilt and unavailable here. No 155 binary was used as 156 qualification. Joined host dispatch/authentication, full GN/header compilation, modal presentation/focus/keyboard ownership, native event routing across real simultaneous browser/utility windows, renderer replacement and actual beforeunload/close durability remain acceptance gaps for independent after-change review and the integration lane. No visible window, owner profile, source checkout/cache mutation, Chromium download/sync/clean, push or PR occurred.

## Retired-generation review correction

Independent review reproduced direct replacement reactivating its outgoing generation. The controller now records outgoing generations before cancellation, including explicit retirement, and rejects every previously retired token. Tokens remain opaque strings with no inferred numeric ordering. The incoming token is copied before callbacks can mutate its source or destroy its owner.

Retirement history is bounded at 1,024 tokens. Exhaustion cancels pending dialogs, removes active authority and refuses further begins or replacements; an owner must establish fresh native authentication before recreating the controller. This avoids either unbounded history or silently accepting an older identity. Regression fixtures reject direct, multiple-hop and explicit-retirement replay while preserving the active renderer and enforcing exhaustion.

The new production-controller regression failed before the correction. Both corrected native sanitizer programs pass, all 38 native source fixtures pass, and all 22 dialog/consumer/module tests pass. No native public signature, JS identity protocol, host/GN/bootstrap registration or shared close-policy change is included.

## Per-request abort and per-window close suspension

The close-warning integration review needs two helper capabilities; both are helper-side only and leave public `confirm`/`prompt` signatures and results unchanged.

Renderer adapter (`createHostDialogs`):

| API | Semantics |
| --- | --- |
| `abortableConfirm(options, signal: AbortSignal)` / `abortablePrompt(options, signal)` | Same validation, queue bound and results as `confirm`/`prompt`. Abort removes only that entry, settles it once as cancel (`{confirmed:false,suppressed:false}` / `null`) and sends `cancelDialog` only for that `dialogId` if begin was dispatched. A late identity reply cannot begin it; a late `shell.dialogResolved` for it is ignored. An already-aborted signal settles as cancel with no native traffic. Never retires the adapter or touches other requests/windows. |
| `suspend(): DialogSuspension` | Returns a fresh frozen token `{settled}`. Refuses new admissions (`ERR_DIALOG`, "Dialogs are suspended for this window") before any identity query. Every pending entry is cancelled once (native cancel only if begun) and its public promise is settled before `suspend` returns; `settled` resolves after those promises' registered continuations. The event listener and `onBeforeClose` hook stay installed. Suspensions are a set: the window stays suspended while any token is outstanding, so overlapping close attempts each hold their own token. More than 64 outstanding suspensions fail closed and keep the window suspended. |
| `resume(token): boolean` | Removes only that token, by object identity; admissions resume once no token is outstanding. Already-released, foreign, forged or post-retirement tokens return false. |

Native (`LinuxDialogController`, forwarded by `LinuxWindowDialogs`):

| API | Semantics |
| --- | --- |
| `Suspend() -> LinuxDialogSuspension` | Opaque, process-unique token. Refuses `Begin` and `Identify` from then on, force-cancels the active surface and every queued entry with one completion each (when called from a completion during an outer cancellation, the outer loop finishes the remaining entries), and prevents the queue pump from presenting anything. `Cancel` and presenter responses stay usable. Does not call `ParentDestroyed`, clear or retire the generation, or touch the 1,024-entry history. Suspension survives `ReplaceRenderer`. Suspensions are a set bounded at `kMaxSuspensions` (64); the window is suspended while any token is outstanding. Counter or set exhaustion fails closed: the window stays suspended permanently and an empty token is returned. |
| `Resume(const LinuxDialogSuspension&) -> bool` | Removes only that outstanding token; admissions resume once none remain. Released, empty or other-window tokens return false. Resuming after parent destruction releases the token but admits nothing. |
| `suspended() const -> bool` | Current state. |
| `Identify(const LinuxDialogClient&, base::DictValue* identity) const -> bool` | Fills `{windowId,rendererGeneration}` only for an authorized, unsuspended client. The host's `ArdiaShell.windowIdentity` handler must reply from this so identity queries are refused while suspended. |

Host hooks (reserved for the integrator): keep each window's `LinuxDialogSuspension` in its close attempt, call `Suspend()` when entering the frozen close phase and `Resume(token)` only with that attempt's token after an authenticated cancellation/veto; answer `windowIdentity` through `Identify`.

Evidence: `/tmp/ardia-f05-suspend-*.log`. JS adapter tests went from 16 to 23 (7 new failed before the change); the native fixtures did not compile before `Suspend`/`Resume`/`Identify` existed and pass under ASan/UBSan after; removing the pump suspension guard fails `TwoWindowsSuspendIndependently`.

Review correction: independent review found the original single-slot suspension let a later `Suspend`/`Resume` pair, including a nested pair inside an outer `Suspend`, release an earlier close attempt's suspension, and that a non-`AbortSignal` argument threw after taking a queue slot. Both now use the set semantics above, and an invalid signal rejects with `ERR_DIALOG` before any slot is taken. Evidence: `/tmp/ardia-f05-fix/` (JS hung on the admitted prompt and native failed to compile before; JS 31/31, native 2/2 ASan/UBSan, default host 136/136 after).
