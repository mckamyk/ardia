# Linux page-state host join (lane A)

Branch `t3/linux-page-state-join`, based on `linux` at `4b3d21e0`. It carries the first-slice helpers (`page-state-first-slice-handoff.md`, cherry-picked unchanged except as noted below) and joins them into the native host, BUILD.gn and the Linux WebView. It implements #33 F23 and #13 F03 at the source level and provides the lease, reset and snapshot substrate that #27 F17 and #35 F25 producers build on.

No Chromium 156 compile, GN check, link or runtime run was possible in this lane. Everything below is source-level and fixture-level evidence only.

## What changed

### Native

- `ac_linux_page_state.{h,cc}` (new): `LinuxPageState` owns one mount's `LinuxPageMountAuthority`, `LinuxPageLease`, `LinuxSecurity`, `LinuxHistoryZoom` and `LinuxTheme`. It composes the creation response and the ordered snapshot (`navigation` with theme fields, `security`, then `zoom` when available) and is the only object the host talks to.
- `ac_linux_page_lease`: adds `EmitPresentation` (validated against the presentation request, so it still works during a provisional navigation), `presentation_contents()` and a one-shot `EmitClosed()` (sends `windowClose` with the original lease metadata after strip removal, then resets the lease; it needs the sink and an un-invalidated authority).
- `ac_linux_history_zoom`: `GoToOffset` and `ZoomStep` act on `presentation_contents()`, so history jumps and zoom keep working while a navigation is pending. Zoom reports still use document requests.
- `ac_linux_host.cc`:
  - `TabBinding` owns `std::unique_ptr<LinuxPageState> page_` plus a contents generation. `RetirePage()` runs before release, reclaim (`CreateTab`), transfer (`MoveTo`), contents replacement (`Rebind`), renderer retirement (`RetireRenderer`), contents destruction and binding destruction, always before the claim changes.
  - `ShellHostView::MountPage` mints a new binding generation from a process-wide counter for every attachment. The sink is bound to the weak host plus the captured handler identity and renderer generation, and re-checks `OwnsHandler` before `Event()`.
  - `tab.create` keeps `browserId`, `chromeTabId` and `state`, and merges `{lease, snapshot}`. If no creation state is available, the claim is cleared and the call fails. The posted ID-only `ReportNavigation` is removed, along with `ShellHostView::ReportTab`.
  - Events now use the envelope: `navigation` (complete, with `committedUrl` and nullable theme), `progress`, `loadError`, `openWindow`, `pictureInPicture` (presentation); `favicon` as `{url, urls}` (document); `windowClose` (terminal). Title changes emit one complete navigation snapshot.
  - `tab.command`: `getSecurityInfo` (zero args; returns the security snapshot, or null with no current document request), `goToOffset`, and `zoomStep` through the helper (real `PageZoom` steps; 0 resets to the actual default).
  - `engine.call` intercepts `sites.zoomLevels` and `sites.setZoom` ahead of the route table, using `PrepareLinuxZoomDomain`, `ac_zoom_list` or `ac_zoom_set`, and `CompleteLinuxZoomDomain`. Invalid requests fail with `ERR_ENGINE`, malformed replies with `ERR_BAD_REPLY`, and service `{error}` replies keep `ERR_ENGINE`.
- `BUILD.gn`: the ten helper files, `//components/security_state/content` and `//skia`.

### Linux UI

- `HostTab` takes an optional page-receiver factory (4th argument; the 3-argument form used by other tests is unchanged). With a receiver, every native event goes to `HostPageEvents.receive`, and `install(createResponse)` runs once. An invalid lease disposes the receiver, releases the native claim and rejects `ready`. `pageResult(command)` captures the request after `ready`, refuses (`null`) when there is none, and drops a result the predicate no longer accepts.
- `HostWebView` drops the legacy callback table. It builds a `HostPageEvents` per claim, and `getSecurityInfo` goes through `pageResult`.
- `hostPageHooks.ts` (new) carries reset and theme application. `main.tsx` registers `resetHostPage` and `applyHostPageTheme`. Importing `hostPageLifecycle` from the WebView shim created the cycle `store → history → ardiacore shim → HostWebView → pageState → store`, which threw `Cannot access 'useBrowser' before initialization`. The SSR component fixture reproduced it, and the graph test now asserts that the shim cannot reach `pageState`.
- `vite.config.ts` registers `SiteConnection.tsx → apps/linux/src/SiteConnection.tsx`, and the wrapper is in the adapter set. Unknown or null security renders "Connection information unavailable", never "Local Page".

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
| `python3 -m unittest discover -s engine/ardiacore-linux/tests -p test_page_state.py` | 16 passed (11 slice + 5 join cases), ASan/UBSan, halt on error |
| native renderer, callback, dialog-dispatch and window-storage fixtures touched by the join | 8 renderer cases, 1 callback case, dialog dispatch and window storage pass |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed |
| `pnpm --filter @ardia/linux typecheck` | passed |
| `pnpm --filter @ardia/linux test:host` | 222 passed (includes `hostPageEvents.test.mjs` 15 and `hostTabPage.test.mjs` 5) |
| `pnpm --filter @ardia/linux test:graph` | 3 passed (module graph plus 2 SiteConnection graph/SSR cases) |
| `pnpm --filter @ardia/linux build` | passed |

The join cases cover: the create response's lease and ordered snapshot (navigation with meta theme, secure security, zoom; zoom omitted after controller destruction; cleared presentation while provisional); a document reset followed by a snapshot on the new epoch; stale-epoch requests rejected by `EmitLinuxPageEvent`; document events suppressed during provisional while navigation and progress still flow; restore snapshot after abort; strictly increasing sequences; `goToOffset` ±2 and ±3 over four entries, including out-of-range no-ops and jumps during provisional; zoom report, step, default reset and rejected step 2; one-shot terminal close; rebinding with a new binding generation; and reentrant teardown from the sink and the navigation callback. Security for https, http, invalid certificate, mixed, internal and unknown (`none`, rendered as unavailable, never local) is covered by the slice's `security` case and the SSR wrapper fixture.

On the base commit, the full native suite already fails `test_chromium_close`, the `test_document_close_*` setup and `test_committed_renderer` cases (the Chromium 156 `unload_controller` patch no longer applies to the fixture tarball). This branch neither fixes nor worsens those; see the PR for the exact comparison.

## Known gaps

- Native contents replacement (`Rebind`, for example a discard) retires the page, but the JS owner is not told to reclaim, so events stop until the tab is re-created. Memory saver and discard are not active on Linux (F18). The F18 owner must add a reclaim notice when enabling them.
- Fullscreen, crash, hung, status, popup and recovery producers (F17), scroll reporting and header sampling (F16/F25) are not added. The snapshot carries only navigation, security and zoom.
- `onReady` now fires after the snapshot replay instead of before the first navigation. Shared `TabWebView` handlers are order-independent.

## Native runtime acceptance still required

Only a Chromium 156 GN, compile and link run, followed by hidden scratch-profile runs, can qualify this. In particular: the `nogncheck` `RenderWidgetHostImpl` include and DEPS policy; `security_state/content` visibility; real HTTPS, HTTP, invalid-certificate, mixed-content and internal pages in the site panel; four-page history with jumps of two or more; zoom keys, the site panel percentage, the nondefault indicator and reset agreeing with native page scale; saved per-site zoom in Settings across restart; meta theme-color changes and clears; BFCache, same-URL reload, provisional abort, release and reclaim, cross-window transfer, renderer reload and tab close while results are pending; and confirming that `window.close()` still closes the Ardia tab through the terminal event.
