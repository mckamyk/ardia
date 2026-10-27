# Desktop investigations R01–R03

Source-level investigation of [#44 R01](https://github.com/mckamyk/ardia/issues/44), [#45 R02](https://github.com/mckamyk/ardia/issues/45) and [#46 R03](https://github.com/mckamyk/ardia/issues/46) on branch `t3/linux-window-lifecycle`, based on `linux` at `4b3d21e0`. None of this was run against a Chromium 156 build or with physical input; the runtime checks each item still needs are listed at the end of its section.

## R01: clicking native split pane B may leave commands targeting pane A

**Finding: a real source-level defect, now fixed in source.**

Shared `TabWebView` activates a split pane through two callbacks: `onPageFocus` and `onPagePress`, both of which call `activateFocusedPane(tabId)`. On macOS the native web view emits `focus` and `pagePress`. On Linux, page viewports are native `views::WebView` children of `ShellHostView`, layered above the shell `WebContents`. A pointer press inside pane B is delivered to B's native view, not to the shell DOM, so `TabPane`'s `onTouchStart` never sees it. Before this change, the Linux host emitted no `pageFocus` event (no `OnWebContentsFocused` observer, and no `"pageFocus"` string anywhere in `ac_linux_host.cc`). `HostWebView` already maps a native `pageFocus` event to `onPageFocus`. That means nothing on Linux updated the shared active pane after a click inside a page.

Fix: `TabBinding::OnWebContentsFocused` reports `{type: "pageFocus"}` to the owning window's renderer. It does so only when the binding is presented in a visible viewport. `activateFocusedPane` already does nothing unless the tab is in a split and not yet active, so programmatic page focus and keyboard pane switching cannot loop.

Tests: `test_window_lifecycle.WindowLifecycleHostTests.test_page_focus_reports_split_pane_activation` (source contract; it failed before the change).

Runtime check still needed: in a split, select A, then physically click and type in B. Confirm that the shared active pane becomes B, and that Reload, Close and Ctrl+L act on B. Repeat after returning from a menu or dialog, after keyboard pane switching, and in a background window.

## R02: cross-window dragging coordinates and per-renderer state

**Finding: the coordinate mismatch is a real defect, reproduced and fixed for X11. Wayland targeting and the per-renderer drag state remain open.**

Shared `state/drag/windowDrop.ts` and `tabDrag.ts` use the macOS frame contract: `[x, y, width, height]` with a bottom-left origin at the primary display, y increasing upward (`screenPoint` computes `frame[1] + frame[3] - y`). Before this change, the Linux host returned top-down `gfx` bounds from `ArdiaShell.windowFrame` and read `openWindow` frames as top-down bounds. It also never reported frames, so the other windows' `frame` stayed `null` and `windowUnder` could not find any target.

`apps/linux/src/windowLifecycle.test.mjs` reproduces the mismatch with the real shared `windowUnder`. With window A at top-down `[0,0,800,600]`, window B at `[900,400,800,600]` and the pointer at screen `(1000,500)`, top-down frames miss B. Frames converted to the shared contract find B, and the local insertion point `(100,100)` inside B is recovered exactly.

Fix: `ac_linux_window_lifecycle` converts between native bounds and the shared contract relative to the primary display:

- `SharedFrame` is used for `windowFrame` replies and frame events.
- `RequestedFrame` handles `openWindow` frames: it accepts integers or doubles, rejects malformed frames and rounds the rest.

Tear-off frames computed by `dropTabsOutside` therefore land where the shared code intends. The compiled fixture covers round trips, offset primaries and rounding.

Still open:

- **Wayland** has no global window coordinates (`supports_global_screen_coordinates == false`). There, `RequestedFrame` keeps only the size, frame events are not emitted, and a cross-window drop can't identify its target from geometry. Over another window it falls back to tear-off. A correct fix needs native drag-and-drop target identity: the window whose surface receives the drag reports itself through a brokered session. That is a separate contract to agree with [#30 F20](https://github.com/mckamyk/ardia/issues/30).
- **Per-renderer state.** `useTabDrag` and the insert resolvers registered by `setInsertResolver` are module-local to each window's renderer. A drag from A over B can't ask B's renderer for its exact insertion row, so `moveTabsToWindow` appends. Exact-row insertion across windows needs a host-brokered drag session.
- **Native previews.** `dragPreview*` stay optional host methods that Linux does not support, so the shared calls no-op.

Runtime check still needed: on X11, drag a tab from A onto B's sidebar and confirm it moves into B (any row). Then tear a tab off onto empty desktop and confirm the new window appears under the pointer. Repeat with a secondary monitor left of the primary and with mixed scale factors. On Hyprland, confirm a tear-off creates a compositor-placed window and that dropping on another window is reported honestly rather than misplaced.

## R03: focus, frame and fullscreen bookkeeping lacks native notifications

**Finding: confirmed missing at source level; notifications implemented.**

The shared `createWindowSync` listener updates `focusedWindowId`, `frame` and `fullScreen` from `onWindowEvent`. Before this change, the Linux host never sent `focus`, `frame` or `fullScreen` events. `ShellHostView` observed the widget only for close handling, and its bounds observer only re-laid out the overlay.

Implementation, all in `ShellHostView` and observing the browser widget it already watched:

- **Focus.** `OnWidgetActivationChanged` feeds the focus ledger. On each transition to active, it sends `{type:"focus", id}` to every regular window's renderer. Every renderer gets it because, as on macOS, every store tracks every window.
- **Frame.** `OnWidgetBoundsChanged` debounces for 250 ms (the macOS resize-report delay), then reports `{type:"frame"}` in the shared contract. It skips fullscreen and minimized windows, repeats, and anything on Wayland.
- **Fullscreen.** `OnWidgetShowStateChanged` reports `{type:"fullScreen", on}` transitions immediately.
- **Utility windows.** Settings and Import publish none of these events.
- **Late renderers.** A renderer whose listeners attach after a change gets a replay of every regular window's current frame, fullscreen and focus when it sends its first own-window `openWindow` (its readiness signal). Focus is replayed last.

Per-renderer authority has no loops: the listener only writes the store, and no store change calls `focusWindow`.

Still open:

- `restoreFullScreen` stays an optional host method that Linux does not support, so fullscreen stamps recorded in the session are not restored at launch.
- Page (HTML element) fullscreen also makes the widget fullscreen, so it is recorded the same way as window fullscreen.
- Wayland sizes rely on Chromium's own window-placement preference because no frame events are emitted there.

Runtime check still needed: with two windows, physically focus, move, resize and fullscreen each window. Compare `useBrowser` state in both renderers, the native bounds and the canonical session file. Restart and confirm sizes and placement on X11, and size-only behavior on Hyprland. Confirm that Settings and Import activation changes nothing in the browser stores, and that `ARDIA_BACKGROUND=1` instances never activate.
