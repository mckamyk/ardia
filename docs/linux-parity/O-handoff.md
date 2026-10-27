# Linux page status, fullscreen, renderer failures, pop-ups and favicons (lane O)

Branch `t3/linux-page-status`. Source implementation for #27 F17 and #34 F24, and the page-vs-window fullscreen split in #78. Tracked by #115.

No Chromium 156 compile, GN check, link or runtime run was possible in this lane. Everything below is source-level and fixture-level evidence only.

## What changed

### Native

- `ac_linux_page_status.{h,cc}` (new): `LinuxPageStatus`, owned by `LinuxPageState` next to security, zoom and theme. It observes the page contents and the tab's `PopupBlockerTabHelper` and produces:
  - `fullscreen {fullscreen}` from `DidToggleFullscreenModeForTab` (presentation channel);
  - `status {text}` from the hovered-link hook, deduplicated, tied to the document it was set on (presentation channel);
  - `crashed {status, reason, code}` from `PrimaryMainFrameRenderProcessGone` (normal and still-running exits are ignored; reason is `abnormal`, `killed`, `crashed`, `oom`, `launchFailed` or `unknown`);
  - `unresponsive {}` / `responsive {}` from the hung-renderer hooks, keeping each hung widget's restarter (`responsive` is sent once no widget of the page is hung; Wait restarts every monitor and Exit Page ends every hung process);
  - `popupBlocked {id, url, origin}` from `UrlListManager::Observer` (document channel, so it is dropped during a provisional navigation and comes back through the restore snapshot). At most 100 are tracked per page.
- Snapshots: `LinuxPageState::SnapshotEvents` appends `fullscreen`, `status` (empty when the hover belongs to an older document), `crashed` when the contents is crashed, `unresponsive` or `responsive`, and one `popupBlocked` per pop-up still blocked on the current document. Snapshots built inside `PrimaryPageChanged` run before Chromium clears its own pop-up list, so pop-ups and status are filtered by document, not by the helper's list alone.
- Commands (`tab.command`): `resolveUnresponsive [terminate]` (Wait runs the restarter; Exit Page shuts the renderer down with `RESULT_CODE_HUNG`), `openBlockedPopup [id, always?]` (`always` allows pop-ups for the committed URL in the page's own content settings, then `ShowBlockedPopup(NEW_FOREGROUND_TAB)`; the new strip entry is offered to the shell through the same `openWindow` event as other opened tabs), `downloadFavicon [url]`.
- Retiring a page (transfer, discard migration, renderer retirement, destruction) stops every producer. A page retired while hung runs the restarter so Chromium reports the hang again to the next page, except while the contents is being destroyed.
- `chromium-linux-page-status.patch` (Linux only, after `chromium-linux-pip-placement.patch`): three function-pointer hooks in `BrowserWebContentsDelegate` (`UpdateTargetURL`, `RendererUnresponsive`, `RendererResponsive`). The host claims them only for contents with a live page state; otherwise Chromium's status bubble and hung dialog run as before. HTML form pop-up hangs are swallowed, as Chromium does.
- #78: `ShellHostView::WindowFullScreen()` reports window fullscreen only when the widget is fullscreen and the fullscreen was not caused by a tab (`LinuxWindowFullScreen`). Tab fullscreen inside a browser-fullscreen window still counts as window fullscreen. The frame report still skips any widget fullscreen.
- F24: `ac_linux_favicons.{h,cc}` and `ac_linux_favicon_policy.h`.
  - `downloadFavicon` uses `WebContents::DownloadImage` on the tab. The request goes through the page's renderer and Blink's fetcher in the page's own profile, so it carries the site's cookies exactly as Chrome's own favicon download does. Only http/https/`data:image/` URLs are accepted, bitmaps are capped at 64 px by the renderer, the closest size is picked like macOS, and the result is a PNG data URL `{uri, width, height}`. The page request is captured at command time; a reply for an older document is null.
  - `favicons.fetch {url}` is intercepted ahead of the route table (like site zoom) and advertised in the bootstrap capabilities. It refuses private windows and profiles the window does not host. It fetches from the browser process with a `SimpleURLLoader` on the window's `page_profile()` storage partition: no cookies, 2 MiB cap, 30 s timeout, and decoding in the image decoder service.
  - `favicons.fetch` stays off the local network. Hosts that are loopback, private, link-local, CGNAT, multicast or reserved literals (IPv4, IPv6 and IPv4-mapped IPv6) and `localhost`, `*.localhost`, `*.local`, `*.internal` and `*.home.arpa` names are refused before any request (`LinuxFaviconPublicURL`, which also asks `net::IPAddress::IsPubliclyRoutable`). Redirects to such hosts abort. After DNS, the request carries a public `ClientSecurityState` with `LocalNetworkAccessRequestPolicy::kBlock`, so the network service refuses names that resolve to a local address.
  - Resizing to 64 px and PNG encoding run on the thread pool for both paths, never on the UI thread.
  - Every reply has a 30 s deadline and resolves null on failure. Requests own themselves, so nothing in flight is dropped when a window goes away.
- `BUILD.gn`: the new files, `//chrome/browser/image_fetcher`, `//chrome/browser/ui/exclusive_access`, `//components/blocked_content`, `//components/image_fetcher/core`.

### Linux UI

- `main.tsx` exposes the page store as `ardiaLinux.pages` for the acceptance suite. `hostPageLifecycle.ts` already mapped every new kind in `RESTORED`; the shared `TabWebView` handlers and overlays (`SadTab`, Page Unresponsive, the pop-up prompt, `useFullscreenTab`) consume the events unchanged.

## Tests run

| Command | Result |
| --- | --- |
| `python3 -m unittest engine/ardiacore-linux/tests/test_page_state.py` | 24 passed (new: `page-status`, `hung`, `popups`; ASan/UBSan) |
| `python3 -m unittest engine/ardiacore-linux/tests/test_page_host_join.py` | 8 passed (new: `status-migration`, producers stop at discard migration and retirement) |
| `python3 -m unittest engine/ardiacore-linux/tests/test_favicon_policy.py` | 3 passed (including local-network host refusal) |
| `python3 -m unittest test_window_lifecycle test_native_callbacks` (from the tests directory) | 14 and 1 passed; the window fixture now covers page vs window fullscreen |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed |
| `pnpm --filter @ardia/linux typecheck` | passed |
| `pnpm --filter @ardia/linux test:host` | 321 passed (new `hostPageStatus.test.mjs`, 5) |
| `pnpm --filter @ardia/linux test:graph` | 3 passed |
| `pnpm --filter @ardia/linux acceptance:self-test` | 48 passed |
| `pnpm --filter @ardia/linux fixture:csp:check` | passed |
| `pnpm --filter @ardia/linux build` | passed |
| `patch --dry-run` of `chromium-linux-page-status.patch` on a copy of the 156 tree's `browser_web_contents_delegate.cc` | applies |

The new native cases did not compile against the base branch (no `LinuxPageStatus`), and the snapshot-size assertions they replace counted three events.

## Acceptance scenarios

`F17` (#27), `W78` (#78) and `F24` (#34) in `apps/linux/scripts/acceptance`, documented in `acceptance-suite.md`. They have not run against a build.

## Native runtime acceptance still required

Only a Chromium 156 GN, compile and link run followed by the scenarios above can qualify this: the patch hooks firing for Ardia's contents, Chromium's hung dialog staying hidden, `ExclusiveAccessManager::From` on Ardia's browser, `Page.crash` and the 30 s hang monitor in headless, `ShowBlockedPopup` inserting into the same strip, the network service honouring the LNA block for `favicons.fetch` (the F24 scenario marks its fixture port public with `--ip-address-space-overrides` and expects a non-overridden loopback port to be refused), `ImageDecoderImpl` decoding ICO, and video fullscreen from a real player.
