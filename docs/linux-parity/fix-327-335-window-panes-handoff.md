# Fix handoff: hidden caption buttons out of the accessibility tree, and the panes follow-ups (#327, #335)

Native in `ac_linux_host.cc`, JS in `apps/linux`, one harness check. macOS, the shared browser and the demo are unchanged.

## Chrome's caption buttons in the accessibility tree (#327)

#317 made Chrome's minimize, maximize, restore and close buttons transparent and stopped them taking events (`SetCaptionButtonsOpacity(0.0f)`). They stayed visible Views, so a screen reader still listed them next to Ardia's own controls and could press them. `ShellHostView::HideCaptionButtons` now also marks the four buttons ignored (`GetViewAccessibility().SetIsIgnored(true)`). It finds them by the view ids `OpaqueBrowserFrameView::InitWindowCaptionButton` gives them (`VIEW_ID_MINIMIZE_BUTTON` … `VIEW_ID_CLOSE_BUTTON`), on the same layout and theme changes that already reapply the opacity. Ignored views aren't exposed to AT-SPI, so they can't be activated through it either.

`test_caption_buttons.py`: the compiled fixture checks all four are ignored, a button added later is ignored on the next layout, and the window title isn't. A new source check pins the view ids in Chromium 156. Needs the next Chromium build, then a manual check with Orca or Accerciser: AT-SPI registers nothing in the #240 harness sessions.

## Panes follow-ups (#335)

- **DevTools focus in a pane that's left (#319).** If pane A's docked DevTools had focus when the user clicked pane B, Chrome's `StoreFocus` on A recorded B's viewport, and `KeepPaneFocus` replaced that with A's page. Coming back to A then focused its page, not its DevTools. The host now listens to its `FocusManager` (`views::FocusChangeListener`, observed from `Initialize` until `OnWidgetDestroying`) and records, per tab, whether its last focused own view was the page or the DevTools viewport. `KeepPaneFocus` restores the DevTools viewport when that was last and DevTools is still docked. `test_pane_focus.py` `devtools-left` fails without it.
- **X11 press after a header drag (#320).** Checked on openbox instead of changed. F26X and F26D now have "right after a header drag the page gets hover and wheel input, not the header". On the 148104a3 engine the page saw 4 pointer moves and 3 wheel events right after the drag, and the shell none, on both openbox and sway. The shell doesn't keep the press.
- **Window-move routes advertised (#320).** The bootstrap advertises `ArdiaShell.startWindowMove` and `ArdiaShell.captionDoubleClick`. `WindowDragRegion` uses them only when advertised, so a UI on an older engine (`fetch-engine --nearest`) doesn't report a host error on every header drag.
- **Drag regions without a host (#320).** In plain Vite dev, or on an engine without the routes, `watchWindowDrag` still runs with no window host: region clicks and region double-click handlers work, and drags and title-bar double-clicks do nothing.
- **Departed pages (#322).** `forgetDepartedPage` read the tab's window from the store at unmount. When a move unmounted the view before the store moved the tab, the old window kept the tab's page state. If the tab still belongs to this window at unmount, the check now waits on the store: when the tab leaves, its state is dropped. If the view mounts again first (`reset("mount")`), the wait is cancelled. `hostPageStatus.test.mjs` covers both, and the unmount wiring from `HostWebView` through `hostPageHooks` to `main.tsx`.

Left on #335: the selection popover's ring shows a still capture while it's up, and every selection takes one full-page capture (#321). Replacing the capture with a live layer needs a design for how the page shows through the ring.
