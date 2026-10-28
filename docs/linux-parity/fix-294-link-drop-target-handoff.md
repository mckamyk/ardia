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
