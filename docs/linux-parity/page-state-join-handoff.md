# Linux page-state host join (lane A)

Branch `t3/linux-page-state-join`, rebased onto `linux` at `bb9c745e` (after #67 and #61). It carries the first-slice helpers (`page-state-first-slice-handoff.md`, cherry-picked unchanged except as noted below) and joins them into the native host, BUILD.gn and the Linux WebView. It is the source-level join for #33 F23 and #13 F03, fixes #84 and #69 (discard), and provides the lease, reset and snapshot substrate that #27 F17 and #35 F25 producers build on.

No Chromium 156 compile, GN check, link or runtime run was possible in this lane. Everything below is source-level and fixture-level evidence only.

## What changed

### Native

- `ac_linux_page_state.{h,cc}` (new): `LinuxPageState` owns one mount's `LinuxPageMountAuthority`, `LinuxPageLease`, `LinuxSecurity`, `LinuxHistoryZoom` and `LinuxTheme`. It composes the creation response and the ordered snapshot (`navigation` with theme fields, `security`, then `zoom` when available) and is the only object the host talks to.
- `ac_linux_page_lease`: adds `EmitPresentation` (validated against the presentation request, so it still works during a provisional navigation), `presentation_contents()`, a one-shot `EmitClosed()` (sends `windowClose` with the original lease metadata after strip removal, then resets the lease; it needs the sink and an un-invalidated authority) and a one-shot `EmitRebind(creation)` built the same way. It tracks every pending primary navigation id, plus a navigation already pending at mount, and leaves the provisional state only once all of them finish without a new document. Snapshots carry their reason (`document` or `restore`). Strip membership checks reuse the last known index and search only after the tab moves.
- `ac_linux_history_zoom`: `GoToOffset` and `ZoomStep` act on `presentation_contents()`, so history jumps and zoom keep working while a navigation is pending. Zoom reports still use document requests. A non-unit page scale with no observed pinch now omits the zoom report; the private `RenderWidgetHostImpl` include and its `nogncheck` are gone. `sites.zoomLevels` skips keys that are not hosts (Chromium keys `file://` zoom by full spec) and levels that do not map to a positive factor. `sites.setZoom` accepts only factors within `blink::kMinimumBrowserZoomFactor..kMaximumBrowserZoomFactor`.
- `ac_linux_security`: describes the last committed entry, and consults visible security state only when the visible entry is that committed entry.
- Snapshots also resend the page's current favicon candidates (`favicon {url, urls}`) when it has any.
- `ac_linux_host.cc`:
  - `TabBinding` owns `std::unique_ptr<LinuxPageState> page_` plus a contents generation. `RetirePage()` runs before release, reclaim (`CreateTab`), transfer (`MoveTo`), renderer retirement (`RetireRenderer`), contents destruction and binding destruction, always before the claim changes. `TabBinding` observes its contents before the page state's observers attach, so its `WebContentsDestroyed` sends `windowClose` while the lease is still live.
  - Contents replacement (`ReplaceTabContents`). Urgent discard is on by default in 156 and swaps the tab through `TabStripModel::DiscardWebContents`. The replacement gets a new `SessionID` (`SessionTabHelper` mints `SessionID::NewUnique`, and `TabModel::DiscardContents` adopts it), so the old id-keyed lookup never matched. The binding then sent `windowClose` when the old contents died, and the shared UI closed the tab. `kReplaced` changes are now resolved by the old contents pointer. The binding re-observes the new contents, is re-keyed to the new tab id and keeps `replaced_id`, so commands already sent with the old id still resolve (the claim must still match). A claimed binding mounts a fresh page state and sends a `rebind` notice through the old lease. Helium does not change urgent discard.
  - Page focus from #61 (`OnWebContentsFocused`, with the `host_focusing_` guard and the presented/visible-viewport conditions unchanged) is sent as the package's `focus` page event through `ReportPresentation`.
  - `ShellHostView::MountPage` mints a new binding generation from a process-wide counter for every attachment. The sink is bound to the weak host plus the captured handler identity and renderer generation, and re-checks `OwnsHandler` before `Event()`.
  - `tab.create` keeps `browserId`, `chromeTabId` and `state`, and merges `{lease, snapshot}`. If no creation state is available, the claim is cleared and the call fails. The posted ID-only `ReportNavigation` is removed, along with `ShellHostView::ReportTab`.
  - Events now use the envelope: `navigation` (complete, with `committedUrl` and nullable theme), `progress`, `loadError`, `openWindow`, `pictureInPicture` (presentation); `favicon` as `{url, urls}` (document); `windowClose` (terminal). Title changes emit one complete navigation snapshot.
  - `tab.command`: `getSecurityInfo` (zero args; returns the security snapshot, or null with no current document request), `goToOffset`, and `zoomStep` through the helper (real `PageZoom` steps; 0 resets to the actual default).
  - `engine.call` intercepts `sites.zoomLevels` and `sites.setZoom` ahead of the route table, using `PrepareLinuxZoomDomain`, `ac_zoom_list` or `ac_zoom_set`, and `CompleteLinuxZoomDomain`. Invalid requests fail with `ERR_ENGINE`, malformed replies with `ERR_BAD_REPLY`, and service `{error}` replies keep `ERR_ENGINE`.
- `BUILD.gn`: the ten helper files, `//components/security_state/content` and `//skia`.

### Linux UI

- `HostTab` takes an optional page-receiver factory (4th argument; the 3-argument form used by other tests is unchanged). The factory receives a `rebound` callback; `HostPageEvents` calls it after accepting a rebind so `HostTab` moves its native id. Commands read the id at send time. With a receiver, every native event goes to `HostPageEvents.receive`, and `install(createResponse)` runs once. An invalid lease disposes the receiver, releases the native claim and rejects `ready`. `pageResult(command)` captures the request after `ready`, refuses (`null`) when there is none, and drops a result the predicate no longer accepts.
- `HostWebView` drops the legacy callback table. It builds a `HostPageEvents` per claim, and `getSecurityInfo` goes through `pageResult`.
- `hostPageHooks.ts` (new) carries reset and theme application. `main.tsx` registers `resetHostPage` and `applyHostPageTheme`. Importing `hostPageLifecycle` from the WebView shim created the cycle `store → history → ardiacore shim → HostWebView → pageState → store`, which threw `Cannot access 'useBrowser' before initialization`. The SSR component fixture reproduced it, and the graph test now asserts that the shim cannot reach `pageState`.
- Resets are reason-aware. Mount, document, rebind and legacy reason-less snapshots clear all page presentation state. A provisional reset clears only security and theme. A `restore` snapshot (the same document after an aborted navigation) clears exactly the state kinds it replays and leaves popups, status, prompts, fullscreen, the popover and the toolbar alone.
- `vite.config.ts` registers `SiteConnection.tsx → apps/linux/src/SiteConnection.tsx`, and the wrapper is in the adapter set. Unknown or null security renders "Connection information unavailable", never "Local Page".

## Rebind notice

`{type: "pageLifecycle", action: "rebind", claim, rendererGeneration, bindingGeneration: <old>, documentEpoch: <old>, sequence: <old stream>, creation: {browserId, chromeTabId, state, lease, snapshot: {watermark, events}}}` arrives on the old outer channel id. `HostPageEvents` accepts it only when it passes the ordinary checks against the current lease and the new lease has the same claim and renderer generation with a strictly larger binding generation. It then installs the new lease, watermark and snapshot (full reset), clears the provisional state, invalidates captured command predicates and reports the new native id.

## Snapshot contract for F17 and other producers

`LinuxPageState::SnapshotEvents` is the one place snapshots are built. A producer that wants its state to survive an aborted navigation, a rebind or a renderer reattach must append its current value there as an ordinary page event (`fullscreen {fullscreen}`, `status {text}`, `crashed {...}`, `unresponsive {}`/`responsive {}`, `popupBlocked` per popup, `contentBlocked {count, url}`, and so on). A restore snapshot replaces exactly the kinds it contains: list every popup, because the popup list is cleared once when any `popupBlocked` is present; send `status {text: ""}` to clear. Omitting a kind keeps the UI's current value. Document and rebind snapshots start from a full reset, so producers must include everything that is still true. `apps/linux/src/hostPageLifecycle.ts` (`RESTORED`) maps event names to the page-store fields they replace; add new kinds there.

## Contract for lane B (find and other async page events)

```cpp
#include "ardia/linux/ac_linux_page_state.h"
bool ardiacore::EmitLinuxPageEvent(const ardiacore::LinuxPageRequest& request,
                                   std::string name, base::Value data);
```

In `ac_linux_host.cc`, capture at command time and hold only the request:

```cpp
auto request = binding->page() ? binding->page()->Capture() : std::nullopt;
if (!request) return std::move(done).Run(false, base::Value("No live page document"));
```

When the asynchronous result arrives (for example a find reply), call `ardiacore::EmitLinuxPageEvent(*request, "find", base::Value(base::DictValue().Set("count", n).Set("active", i).Set("final", f)))`. It returns false and sends nothing once the document, epoch, request generation, claim or binding has moved on, or during a provisional navigation. Never keep `TabBinding*`, the lease or an ID and look it up again later. The JS side already dispatches `find` to `onFindResult` through the real package dispatcher.

For events that must keep flowing during a provisional navigation, `binding->ReportPresentation(name, dict)` is the binding-level entry.

## Tests run

| Command | Result |
| --- | --- |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | 244 passed, including `test_page_state.py` 20 (ASan/UBSan, halt on error), `test_page_host_join.py` 4, `test_window_lifecycle.py` from #61 and the close fixtures from #67 |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed |
| `pnpm --filter @ardia/linux typecheck` | passed |
| `pnpm --filter @ardia/linux test:host` | 231 passed, including `hostPageEvents.test.mjs` 17 and `hostTabPage.test.mjs` 8 |
| `pnpm --filter @ardia/linux test:graph` | 3 passed (module graph plus 2 SiteConnection graph/SSR cases) |
| `pnpm --filter @ardia/linux build` | passed |

`test_page_host_join.py` compiles the real page-state helpers together with `TabBinding` and `ShellHostView` methods extracted from `ac_linux_host.cc`. It covers: binding-before-lease observer order, so `windowClose` uses the live lease; a discard swap that re-keys the binding to the new id, mounts a new lease and sends the rebind notice; old-id commands resolving only with the current claim; no `windowClose` when the old contents dies; a swap during a pending navigation, which sends a cleared snapshot; and unclaimed, renderer-retired and id-colliding swaps.

The helper cases cover:
- the create response and its ordered snapshot, including favicons;
- document resets with the `document` reason, and aborted navigations with the `restore` reason;
- stale epochs rejected by `EmitLinuxPageEvent`;
- overlapping primary navigations, and security taken from the committed entry rather than a pending one;
- `goToOffset` ±2 and ±3 over four entries;
- zoom report, step and reset, the bounded `sites.setZoom` factor, and a `file://` key in `sites.zoomLevels`;
- an untracked pinch producing no report;
- strip-index reuse;
- terminal close, rebinding and reentrant teardown.

Security for https, http, invalid certificate, mixed, internal and unknown (`none`, rendered as unavailable, never local) is covered by the `security` and `committed-security` cases and the SSR wrapper fixture.

## Known gaps

- Fullscreen, crash, hung, status, popup and recovery producers (F17), scroll reporting and header sampling (F16/F25) are not added. The snapshot carries navigation, security, zoom and favicons.
- The shared `onDiscarded` callback is not sent on discard; after a rebind the UI shows the discarded tab's navigation snapshot and reloads on activation.
- While a pinch zoom has not been observed since mount, a non-unit page scale is not reported.
- `onReady` now fires after the snapshot replay instead of before the first navigation. Shared `TabWebView` handlers are order-independent.

## Native runtime acceptance still required

Only a Chromium 156 GN, compile and link run, followed by hidden scratch-profile runs, can qualify this. In particular: `security_state/content` visibility; an urgent discard (`chrome://discards`) of a background tab and its reload on activation, with the Ardia tab staying open; real HTTPS, HTTP, invalid-certificate, mixed-content and internal pages in the site panel; four-page history with jumps of two or more; zoom keys, the site panel percentage, the nondefault indicator and reset agreeing with native page scale; saved per-site zoom in Settings across restart; meta theme-color changes and clears; BFCache, same-URL reload, provisional abort, release and reclaim, cross-window transfer, renderer reload and tab close while results are pending; and confirming that `window.close()` still closes the Ardia tab through the terminal event.
