# #294: a page's link drop opens a tab only when the drop landed on that page

Issue: [#294](https://github.com/mckamyk/ardia/issues/294), from #285's review and the pages lane's F16D (#240). Branch `fix2-drop-window`.

## What macOS does (parity reference)

- **The source page's `linkDrop`.** The page script sends it on `dragend` when `dropEffect` is `none` and the end point is inside the page (`packages/ardiacore/ios/page_script.js:1093-1102`). The host opens one foreground tab only if the window under the mouse is the page's own window and the mouse is inside the page view (`packages/ardiacore/ios/ArdiaWebView+PageMessages.mm:166-181`, `windowNumberAtPoint … == window.windowNumber && NSPointInRect(…, self.bounds)`). A drop on another window or outside every window opens nothing through this path.
- **A drop on another page, such as a second Ardia window.** Blink's `DragController` handles it in the target page. `OperationForLoad` refuses only the drag's own page, editable documents and plugin documents (`third_party/blink/renderer/core/page/drag_controller.cc:571-573`). It opens the link as a new foreground tab from the target page (`:372-380`, crbug 451659). That tab goes through the target tab's delegate (`engine/ardiacore/src/ardia/core/ac_web_contents_delegate.mm:61-90`), so it opens in the target's window and profile. The macOS engine is the same Chromium 156 (`docs/engine-build.md:4`), and no patch in `engine/patches` touches this path. Because the target accepted the drop, the source's `dropEffect` is not `none` and it sends no `linkDrop`.

So "drop a link on a second Ardia window → one foreground tab there, in that window's profile" is macOS behaviour, and Linux already matches it through the same Blink path. F16D's "opens nothing" expectation for that case should become "one tab in the second window, the first window unchanged". The `first:1, second:1` it reported reads one global store, so it counts one tab twice.

## What diverged on Linux

On 0f5d71dc (with #285), F16D's "drop target or outside the window" check opened a tab. A split run of that check: 0 tabs from the in-page drop target, 1 from the drop outside the window. `NoteLinuxDragEnded` decided "over the page" from `display::Screen::GetCursorScreenPoint()` and the tab's `GetContainerBounds()`. On Wayland every window's origin is 0,0, and the cursor point is the last one the focused window saw, so a drop outside the window (or on another window) still read as inside the page. On macOS the same `linkDrop` opens nothing.

## Fix (Linux native only)

- `LinuxDropWatch` (`ac_linux_page_channel.cc`): when a page gets a left press, the page channel observes the `aura::client::DragDropClient` of that page's root window, once per client. A watch drops its client in `OnDragDropClientDestroying`.
- `OnDragCompleted` fires on the client of the window that takes the drop, before the source's drag ends (Wayland's `OnDragDrop` precedes `dnd_finished`; on X11 `XdndDrop` precedes `XdndFinished`). `NoteLinuxDrop` hit-tests the drop's root location in that root window and finds the page view that contains the hit window, or none (the sidebar, another window's chrome). It passes that page's surface to the gate.
- `LinuxLinkGestureGate::NoteDrop(target)` records it only while a press is open. A new press resets it. `NoteDragEnded(surface, now)` counts the drag as over the page only when the last drop of that drag landed on the source page itself. No cursor coordinates are used.
- Result: the source page's `linkDrop` opens its one foreground tab only for a real drag that was dropped back on its own page, as on macOS. Drops on another window are left to the target page's own Blink handling, which opens in that window's profile. A private or isolated page's link therefore never opens through the source page in a regular window, and the other way round. Drops outside every window open nothing.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_page_messages` | 9 passed. `link-drops` fails to compile on `linux`. New checks: no drop, a drop on another page, a last drop on the shell, and a drop before the drag's press all open nothing; a drop on the source page opens |
| `python3 -m unittest test_page_channel_contract` | 5 passed. The drag end no longer reads the cursor; drops are hit-tested from the drag client |
| `bash scripts/linux/syntax-check-156.sh ac_linux_page_channel ac_linux_link_gesture ac_linux_host.cc` | 3 of 3 pass |
| `test_private_links`, `test_page_world`, `test_page_commands`, `test_linux_features` | OK |

## F16D on 0f5d71dc (before this change)

- "dragging a link onto its own page with no drop target opens one foreground tab on the link": pass (#271 is fixed).
- "dragging a link onto a drop target in the page or outside the window opens nothing": fail. The in-page target opened 0, outside the window opened 1. This change fixes the outside case.
- "dragging a link onto a second Ardia window …": did not reach the drop. "clicking tab main-tab-0's page 3 times never gave it keyboard focus" is a harness focus step. Its expectation should follow the parity section above.

## Needs the next build to confirm

F16D's own-page drop still opening one tab, the outside-window drop opening nothing, and the second-window drop opening exactly one tab in the second window (target profile, first window unchanged).

## Regression on cand-980e7f14: the own-page drop never reaches a drop target

F16D on the cand-980e7f14 package (with #301) failed "dragging a link onto its own page with no drop target opens one foreground tab". It had passed on 0f5d71dc. A run of the packaged `chrome` with gdb attached after startup (symbols from the matching unstripped build, same build ID) traced `DesktopDragDropClientOzone`'s `StartDragAndDrop`, `OnDragLeave`, `OnDragDrop` and `OnDragFinished`, `LinuxDropWatch::OnDragCompleted`, `NoteLinuxDragEnded` and `AcceptLinkDrop`. Launching under gdb kept DevTools from starting, so the wrapper starts `chrome`, waits for `DevToolsActivePort`, then execs `gdb -p` as its parent (Yama ptrace scope 1).

| Drag | Sequence (ms after drag start) |
| --- | --- |
| own page, no drop target | page leave 849.3, source finished (cancelled) 851.5, drag ended 855.1, `linkDrop` checked; no `OnDragDrop`, no `OnDragCompleted` |
| page drop target | `OnDragDrop` 856.4, finished (copy) 858.4 |
| outside the window | page leave 272.9, source finished (cancelled) 980.2 |
| second Ardia window | page leave 90.2, the other window's `OnDragDrop` 980.5, finished (copy) 982.7 |

Blink refuses a drop on the page that started the drag (`drag_controller.cc:571-573`), so the platform offer accepts no action. On release sway sends the target `leave` and the source `cancelled`, not `drop`. X11's XDND does the same (`XdndLeave` instead of `XdndDrop`). The page only sends `linkDrop` when `dropEffect` is `none`, which means a refused or cancelled drop. So `OnDragCompleted`, which fires only for a delivered drop, never fires in the case `linkDrop` exists for.

At the protocol level, a release over the page and a drop outside the window differ only in when the page saw the drag leave. At release, the leave comes in the same Wayland dispatch as `cancelled` (2 ms apart under gdb). When the pointer leaves first, the leave comes long before the release (707 ms here). Sway sends no pointer `enter` after a drop without motion (the next page input came 6 s later, from the harness), and `linkDrop` arrives before any later pointer input anyway.

## Fix

- `LinuxPageDropTrack` (`ac_linux_page_channel.cc`) wraps the `aura::client::DragDropDelegate` that `WebContentsViewAura` installs on the page view's window (`web_contents_view_aura.cc:1078`). It is installed when the page gets a left press and forwards all four calls.
  - Entered and updated mark the drag over the page.
  - Exited posts a task that settles the exit.
  - It restores WCVA's delegate and deletes itself when the window is destroyed or the WebContents is destroyed. The latter is notified before the frame tree and view are torn down, so it never outlives WCVA.
- `LinuxDragOver` (`ac_linux_link_gesture.h`) is the pure state: an exit counts only once its task has run, and a stale exit does not undo a later entry.
- The drag ends inside the nested drag loop's dispatch, before queued tasks run, so a leave that came with the release is still unsettled when `NoteLinuxDragEnded` runs. `NoteLinuxDragEnded` calls `Gate().NoteDrop(over ? source : 0)` before `NoteDragEnded`. A pointer that left earlier has settled, and its drop opens nothing.
- `LinuxDropWatch` and `NoteLinuxDrop` are removed. Cross-window drops stay with the target page's Blink handling, as on macOS.

| Command | Result |
| --- | --- |
| `python3 -m unittest test_page_messages` | 10 passed. `drag-over` replays the own-page sequence (enter, exit, end before settle), the outside sequence (exit settled before the end) and a re-entry; it fails to compile on `linux` |
| `python3 -m unittest test_page_channel_contract` | 5 passed |
| `bash scripts/linux/syntax-check-156.sh ac_linux_page_channel ac_linux_link_gesture ac_linux_host.cc` | 3 of 3 pass |
| `test_private_links`, `test_page_world`, `test_page_commands`, `test_linux_features` | OK |

Needs the next build: all four F16D drop checks.
