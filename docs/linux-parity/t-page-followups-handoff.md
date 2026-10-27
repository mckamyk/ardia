# Linux page messaging and page-status follow-ups (lane T, part 1)

Branch `t3/linux-page-followups`. Source-level work for #129, #132, #133, #134, #135 and #143, plus the native half of #125. Tracked by #164. No Chromium 156 GN, compile, link or runtime run was possible in this lane: everything below is source-level and fixture-level evidence. The media and tab-policy half is on `t3/linux-media-followups` (`docs/linux-parity/t-media-policies-handoff.md`, PR #176).

## What changed

### Hung renderer without a current page (#129)

- `ac_linux_page_status`: `DeferLinuxHang`, `ResolveDeferredLinuxHang`, `ForgetDeferredLinuxHangs` and `DeferredLinuxHangs` keep hangs per `WebContents` (widget id → restarter, at most 64 per contents, destroyed widgets pruned).
- The `RendererUnresponsive` hook hands the hang to a live page as before. If no page took it and any Ardia window holds the tab (bound, or still in its strip while unclaimed or transferring), it defers the hang and returns `true`, so Chromium's hidden hung dialog never runs.
- `LinuxPageStatus::AppendSnapshot` adopts every deferred hang whose widget is still unresponsive, so the next document, restore or rebind snapshot carries `unresponsive` and the shell shows Page Unresponsive as soon as the tab is presented. Wait and Exit Page then work as before.
- A page retired while hung (transfer, renderer retirement, discard migration) defers its hangs instead of restarting the monitor. `RendererResponsive` drops a deferred hang. `TabBinding` forgets them when the contents is destroyed or replaced.

### Link drops (#134)

- `chromium-linux-page-status.patch` adds `g_ardia_linux_drag_ended(BrowserWindowInterface*)` to `BrowserWebContentsDelegate::HandleDragEnded`. Chromium calls it from `WebContentsImpl::SystemDragEnded` on the drag source, after `DragSourceEndedAt` and before the renderer fires `dragend`. The patch still applies with zero fuzz to the 156 file.
- The press that starts a renderer drag reaches the page widget's input observer, but the release goes to the drag-and-drop client. `LinuxLinkGestureGate` therefore treats a press with no release as the drag source. On drag end the host checks that the source tab belongs to the reporting window and whether the cursor is inside the tab's view (`GetContainerBounds`), then records it.
- `linkDrop` is a new allowed kind (primary main document only, `{url}` rebuilt from an http(s) URL of at most 8192 bytes). It opens only once per drag, within 2 s, on the page the drag started from, only if the drag ended over that page, only while the page is shown, and only if nothing was pressed since. It emits `openWindow {url, disposition: "foreground", userGesture: true}`, as macOS does.

### Native input activity (#125, native half)

- The page channel already observes every page widget and the shell. Keyboard, mouse, wheel and touch input note activity in `LinuxInputActivity` (at most one announcement per 0.5 s). The host posts `ArdiaEngine` `engine {name: "engine.inputActivity"}` to every browser window renderer. The shell side that feeds it into the idle clock is in the media and tab-policy PR.

### Ardia's isolated world (#143)

- Before its first install in a renderer process, the page agent calls `blink::SetIsolatedWorldInfo` once for `ISOLATED_WORLD_ID_ARDIA_PAGE`, with CSP `script-src 'none'; object-src 'none'` and a unique opaque security origin.
- A world with its own policy bypasses the page's CSP and Trusted Types (`ShouldBypassMainWorldDeprecated`), so the password-eye `innerHTML` and its shadow-root style work on Trusted Types and strict-CSP sites. The world's policy still stops it from running `eval` or injecting script and plugin elements.
- Why opaque: Blink requires an origin with a world CSP, and the origin is process-wide for the world id. An opaque origin gives the world no network or storage identity of its own, and `page_script.js` makes no requests. The shared V8 security token only lets Ardia's own world contexts in one process reach each other, as content scripts of one extension already do. DOM, cookies and storage keep each document's own origin.

### Shared evaluate callers (#133)

- `hostPageScripts.ts` maps the engine-switch unsaved-work probe (`engines/actions.ts`) to the native `unsavedInput` command, like the PiP probes. A test pins the literal to the shared source.
- Translate never reaches a page on Linux (no translation backend, so `translation` is null). The Apple Events JavaScript bridge is macOS-only and refuses. Selection, PiP state and close, and the unsaved-input checks were already mapped. The extension Web Store bridge and the popup size probe belong to the extensions lane and are tracked in a follow-up.

### Drift check (#135)

- `apps/linux/src/pageOperations.test.mjs` runs in `test:host`. It compares `ac_linux_page_operations.h` with `pageOperationsHeader()` row by row. It also loads the real shared `selection.ts` and `commands.ts` with a recording WebView and proves that `jumpToSelection`, `copyPageUrl`, `copyLinkToSelection`, `replaceInField` and `useSelectionForFind` send exactly the derived texts, and that the replace call's argument tail is the JSON the host re-serializes.

### Acceptance (#132)

- `F16` (#26) and `F25` (#35) scenarios with `/gestures`, `/gesture-frame` (cross-site) and `/tint/{meta,header,background,plain}` fixtures. The checks and manual items are in `acceptance-suite.md`.
- `main.tsx` exposes `ardiaLinux.toolbar` (toolbar auto-hide store) and `ardiaLinux.selection` (`state`, `jump`, `copyQuoteLink`, `replaceInField`) for the suite.

## Contract

| Item | Contract |
| --- | --- |
| Page message `linkDrop` | `{url}` from the active primary main document; opens only through the drag gate |
| Page event `openWindow` | adds `disposition: "foreground"` for link drops |
| Engine event | `ArdiaEngine/engine {name: "engine.inputActivity", data: {}}`, at most twice a second, to every browser window |
| Chromium hook | `void (*g_ardia_linux_drag_ended)(BrowserWindowInterface*)` |
| Hung renderer | an Ardia-held tab never falls back to Chromium's dialog; the hang appears in the next live snapshot |
| Isolated world | `ISOLATED_WORLD_ID_ARDIA_PAGE`: opaque origin, CSP `script-src 'none'; object-src 'none'` |

## Tests run

| Command | Result |
| --- | --- |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | 444 passed |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 8 passed |
| `pnpm --filter @ardia/linux typecheck` | passed |
| `pnpm --filter @ardia/linux test:host` | 378 passed (new `pageOperations.test.mjs`) |
| `pnpm --filter @ardia/linux test:graph` | `page-state-components.test.mjs` fails identically on `origin/linux` (the #160 credential-wording plugin rejects its partial build); not caused by this lane |
| `pnpm --filter @ardia/linux build` | passed |
| `pnpm --filter @ardia/linux acceptance:self-test` | 65 passed |
| `patch -p1 -F0 --dry-run` of `chromium-linux-page-status.patch` on a copy of the 156 `browser_web_contents_delegate.cc` | applies |

New native cases: `deferred-hang` and updated `hung` (`test_page_state`), `link-drops` and `activity` (`test_page_messages`), the link-drop and activity contracts (`test_page_channel_contract`), `test_page_world`, and the hang forgetting in `test_native_callbacks`. Each failed (did not compile, or asserted) before its change.

## Needs native runtime acceptance on a Chromium 156 build

None of this has been compiled or run against Chromium 156:

- GN and compile of the new hook, `display::Screen` in the page channel, and `blink::SetIsolatedWorldInfo` in the agent.
- That `HandleDragEnded` fires for Ardia's tabs on Ozone (X11 and Wayland), that the press reaches the page widget before the drag starts, and that `GetCursorScreenPoint` is current after a Wayland drag. Headless Ozone has no drag loop, so link drops are a manual acceptance item.
- That Chromium's hung dialog stays hidden for an unclaimed or transferring tab, and the overlay appears when it is shown again.
- That the password eye works on a Trusted Types site, and that `page_script.js` behaves the same with the world policy.
- The F16 and F25 scenarios, which have only been self-tested.

## Left out (tracked)

- #168: the page script's main-world shims (Notification, mediaSession, getDisplayMedia, autoplay) still patch only Ardia's isolated world.
- #169: the extension Web Store bridge and popup size probe still use evaluate (extensions lane).
- #125 is finished together with the media and tab-policy PR, which feeds `engine.inputActivity` into the idle clock.
