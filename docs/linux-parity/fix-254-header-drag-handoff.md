# #254: the sidebar header moves the window and double-click maximizes it

Issue: [#254](https://github.com/mckamyk/ardia/issues/254), found by the desktop-session harness (#240, wave 2 profiles lane, F26D/F26X). Branch `fix2-header-drag`.

## Cause

On macOS `WindowDragRegion` is a native view: a press on it starts `performDrag`, a double-click runs the system title-bar action, and `clickable` regions click on release or drag past 3 points. The Linux shim rendered a plain `View` that only forwarded `onClick` and `onDoubleClick`, so no Ardia drag region moved the window.

The toolbar's empty strip only moved the window by accident: the shell covers Chromium's hidden horizontal tab strip, and `BrowserView::NonClientHitTest` answers `HTCAPTION` for its empty part, which `WindowEventFilterLinux` turns into a move and a double-click frame action. At the sidebar header (x 140) the hidden strip has a tab, so the hit test answers `HTCLIENT`.

## Fix

- **Shell** (`apps/linux/src/windowDrag.ts`, used by the `WindowDragRegion` shim): a left press on the region itself (any descendant for `clickable` regions; never with Ctrl, like macOS) is taken by the region. Moving 3 px with the button held asks the host to move the window; for `clickable` regions a release without that movement calls `onRegionClick`. A second press of a double-click (`detail === 2`) runs the title-bar action, or `onRegionDoubleClick` when `titleBarDoubleClick` is false (the new-tab double-click on the sidebar's empty space). `clickable` regions never double-click, as on macOS.
- **Host** (`ac_linux_window_caption.cc`, two methods on the calling window): `ArdiaShell.startWindowMove` hands the press to the window manager through the platform window's `WmMoveResizeHandler::DispatchHostWindowDragMovement(HTCAPTION, …)` (xdg_toplevel `move` on Wayland, `_NET_WM_MOVERESIZE` on X11), the same call `WindowEventFilterLinux` makes for Chromium's own caption. It refuses when no mouse button is down or the window is fullscreen. `ArdiaShell.captionDoubleClick` runs the desktop's double-click frame action from `ui::LinuxUi` (toggle maximize by default; minimize, lower, menu or nothing as configured), and won't maximize a window that can't (Import).

The shared browser and macOS are unchanged.

## Tests

| Command | Result |
| --- | --- |
| `node --test src/windowDrag.test.mjs` (in `test:host`) | 5 passed; fails before (no module): threshold drag, click vs double-click, region double-click handler, ignored presses, `clickable` click/drag, reported failures and cleanup |
| `pnpm --filter @ardia/linux typecheck`, `test:host`, `test:graph`, `build` | pass |
| `bash scripts/linux/syntax-check-156.sh ac_linux_window_caption ac_linux_host.cc` | pass |

## Needs the next build to confirm

The UI calls host methods the shipped engines don't have, so F26D (sway: dragging the sidebar header moves the window) and F26X (openbox: drag moves it, double-click maximizes and restores) need a package built from this branch.
