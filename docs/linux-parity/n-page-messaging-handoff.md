# Linux page messaging, link gestures, selection tools and tint (lane N)

Branch `t3/linux-page-messaging`. Source-level implementation for #26 F16 and #35 F25, tracked by #117. No Chromium 156 GN, compile, link or runtime run was possible in this lane; everything here is source and fixture evidence.

## Trust model

- Pages never get a shell bridge. The shared `page_script.js` runs in Ardia's own isolated world (`ISOLATED_WORLD_ID_ARDIA_PAGE`, Linux only) installed by a renderer agent at document start for http, https and file documents. The page's main world cannot see the script's `post` function or its configuration.
- Everything the renderer posts is untrusted. The browser channel (`LinuxPageChannel`) takes the sender from `RenderFrameHostReceiverSet::CurrentTargetFrame()`, never from the payload. It accepts an allowlist of kinds (`hello`, `peekHello`, `peekGone`, `peek`, `selection`, `scroll`, `theme`), bounds kind (32 bytes), payload (64 KiB) and depth (4), and rebuilds each payload from validated fields. `scroll`, `selection` and `theme` are accepted only from the active primary main document; iframes may only request peek. Back-forward-cached, prerendered and inactive documents are silent.
- Flooding: each sending document, main frame or iframe, has its own token bucket (64 messages, 32/s; 512 KiB, 256 KiB/s) held as `DocumentUserData`. 256 refusals mute only that document, for the rest of its life. An iframe that floods, or spawns many sub-iframes, cannot mute the top page.
- Peek and split only open Ardia UI after a real left mouse-up, observed through `RenderWidgetHost::InputEventObserver` on the tab's widgets, with exactly the claimed modifiers, over the same tab, within 10 s, unused and with no press since (presses in the shell also count). Refusals fall back to what the click would have done only for a fresh matching click. This mirrors `ArdiaWebView+Peek.mm`.
- Events reach the shell through the page lease (`EmitDocument`), so they are dropped after a document change, during a provisional navigation, and after release or rebind. The sampled tint is tied to the sending document's `WeakDocumentPtr`; native meta theme-color still wins.
- Pages read nothing from the shell except the peek and split modifiers and the selection-popover flag. When a prerendered or back-forward-cached page becomes primary, the channel greets every frame of the new primary page, because its document-start `hello` was refused while it was not primary.
- The isolated world has no `SetIsolatedWorldInfo`, so it inherits the page's CSP and Trusted Types. On Trusted Types sites the page script's password-eye `innerHTML` throws (caught by the script's guard). Its main-world shims (Notification, mediaSession, getDisplayMedia, autoplay) only patch the isolated world's own objects. Scroll, selection, peek and theme are unaffected. A separate world CSP needs a process-wide per-world security origin; tracked in #143.
- Selections are bounded at 4000 UTF-16 units, as on macOS, so long CJK and emoji selections behave the same.
- Evaluation is not generic. `tab.command evaluate` runs only the shared selection operations (`selectionContext`, `hasSelection`, `jumpToSelection`, `replaceInField`, `selectedText`), pinned by byte length and SHA-256 in `ac_linux_page_operations.h`. Replace arguments are parsed, typed, bounded and re-serialized by the host. Scripts run in the isolated world of the captured primary main document, and the result is dropped if the document changed. Anything else is refused with `ERR_PAGE_EVALUATION_REFUSED`, and the shim resolves it to `null`.

## Contract

- Domain routes `webView.setPeekModifier`, `webView.setSplitLinkModifier` and `webView.setSelectionPopover` are handled natively and advertised as capabilities. Changes are pushed to live pages.
- Page events: `pageMessage {kind: "scroll", data: {state}}`, `pageMessage {kind: "selection", data: {text, rect} | null}`, `openWindow {url, disposition: peek|split|window|background, userGesture: true, point?}`, and `navigation` with `themeColorSource` of `meta`, `header` or `background`.
- Snapshots replay the current document's last scroll state and selection (at most eight extra events).
- A contents replacement (`ReplaceTabContents`, urgent discard) detaches the old contents' channel in `Rebind`, so the old page's messages never reach the new tab.
- `apps/linux/scripts/page-operations.mjs --check|--write` keeps the evaluation table in step with `selection.ts` and `commands.ts`.
- Chromium: `engine/patches/chromium-linux-page-agent.patch` (Linux-only world id, renderer agent hooks, browser binder pointer). It applies with zero fuzz to the 156 tree. `apply.sh` copies `page_script.js` into `ardia/core` for the `page_script` GN action.

## Tests run

| Command | Result |
| --- | --- |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | 337 run; the two failures from that run (host-join fixture stub, GN source listing) were fixed and those modules re-run green: `test_page_host_join`, `test_page_commands`, `test_native_callbacks`, `test_bootstrap_build` (34), `test_downloads`, `test_page_messages` (7, ASan/UBSan), `test_page_state` (23) |
| `python3 -m unittest discover -s engine/patches/build/tests` | passed |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed |
| `pnpm --filter @ardia/linux typecheck` | passed |
| `pnpm --filter @ardia/linux test:host` | 316 passed |
| `pnpm --filter @ardia/linux build` | passed |
| `node apps/linux/scripts/page-operations.mjs --check` | passed |
| `patch -p1 --dry-run -F0` of the Chromium patch on copies of the 156 files | applies |

## Not done

- No compile of the renderer agent, the mojom, the channel or the host hunks against Chromium 156.
- No acceptance scenarios under `apps/linux/scripts/acceptance` yet.
- `linkDrop` (dragging a link out of a page to open it) is not accepted. A trusted signal for the end of a renderer drag is still needed.
- Other shared `evaluate` and `executeJavaScript` callers (translate, unsaved-input checks, PiP state, the extension store bridge) are refused, as they were before this change.
- The operations drift check is not wired into a package script or CI, and no test yet compares the derived strings with the runtime strings.

## Native runtime acceptance still required

On a 156 build with hidden scratch-profile runs:
- Shift-click on a link opens peek, and Alt-Shift-click opens split.
- Shift-click on a link inside a cross-site iframe opens peek, through the OOPIF widget path.
- A page that synthesizes clicks or messages opens nothing.
- Scrolling hides and shows the toolbar, and each setting turns it off.
- Pages without meta theme-color get header and background tints that reset on navigation.
- The selection popover, quote link, jump to selection and replace in field work, and Escape dismisses the popover.
- Page JavaScript cannot see `__ardiaHost` or `post`.
