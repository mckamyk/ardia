# Fix handoff: a Wayland fullscreen window comes back on the output it was saved on (#249)

Branch `linux-wayland-output`, based on `linux` at `f1b72558`. Native only: `ac_linux_host.cc` and `ac_linux_window_lifecycle.{h,cc}`. No UI, shared or macOS code changed.

## Root cause

On Wayland a client can't read or set its global position, so the host sends no `frame` events and the store saves no position (`f22-window-lifecycle-handoff.md`). Fullscreen restore (`ArdiaShell.restoreFullScreen`, `ac_linux_fullscreen_restore`) calls `ToggleBrowserFullscreenMode`. That sends `xdg_toplevel.set_fullscreen` with no output, so the compositor puts the window on whichever output it maps on. In W77D that is HEADLESS-1, not the HEADLESS-2 it was saved on.

## Fix

**What is saved.** The host keeps the window's output label: the `wl_output` description Ozone puts in `display::Display::label()` (sway: `<make> <model> <serial> (HEADLESS-2)`). Wayland display ids are `wl_registry` names and change between runs; the label doesn't. The label lives in the app profile's Chrome window-placement pref, under `browser.app_window_placement.ArdiaLinuxWindowOutputs`, keyed by Ardia window id. The shared store and its session file are untouched.

**When it is saved.** Only on Wayland, for regular non-private windows, while the window is browser-fullscreen (`WindowFullScreen()`):
- on every geometry report (`ReportWindowGeometry`, which runs on show-state changes and 250 ms after bounds changes);
- when the widget closes (`OnWidgetClosing`), which covers quitting with Ctrl+Shift+Q.

Nothing is saved while a fullscreen restore is in progress (`FullScreenRestorer().active()`): at that point the window is still on the compositor's first choice. The entry is removed when a window departs the session (`CommitWindowDeparture`).

**Restore.** `RestoreFullScreenNow` reads the saved label before Chrome's toggle and matches it against `display::Screen::GetAllDisplays()` (`OutputLabelled`: exactly one display with that label). After `ToggleBrowserFullscreenMode`, it calls `Widget::SetFullscreen(true, <that display id>)`. On Wayland that reaches `xdg_toplevel.set_fullscreen(<wl_output>)` through `WaylandToplevelWindow::SetWindowState`. Ozone lets a fullscreen request with a target through even when already fullscreen. If the output is gone, or two outputs share the label, the window stays fullscreen wherever the compositor put it, as before.

Event order on relaunch:
1. The window maps on the compositor's output.
2. The renderer calls `restoreFullScreen`, and `RestoreFullScreenNow` reads the saved label.
3. Chrome's toggle requests fullscreen with no output; `BrowserView::ProcessFullscreen` is synchronous without `AsyncFullscreenWindowState`, which is off by default in 156.
4. The host requests fullscreen on the saved output.
5. The compositor handles both requests in order. sway's fullscreen request handler moves a container to the named output's workspace; that is the step W77D has to confirm on a build.
6. The show-state change marks the restore entered. The saved label is not overwritten, because the restore is still active when that report runs.

X11 is unchanged: its saved frame already places the window before fullscreen.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `test_window_lifecycle.py` `fullscreen-output`: saved only while fullscreen; not for utility windows; forgotten on departure; restore targets the saved display; a report during the restore doesn't overwrite it | fail | pass |
| `fullscreen-output-gone`: a missing output falls back to the plain toggle | fail | pass |
| `x11-outputs`: nothing saved with global coordinates | fail | pass |
| `output-labels`: exactly one display must carry the label | fail | pass |
| Structural: saved on close, forgotten on departure | fail | pass |
| `scripts/linux/syntax-check-156.sh ac_linux_host.cc ac_linux_window_lifecycle.cc` | | pass |

Removing the retarget, the restore-in-progress guard or the recording call each makes a test fail.

## Needs the next build

Not compiled into a package yet. On the next build, run W77D (two-output headless sway). Also check by hand on Hyprland with two monitors that the compositor honours the output in `set_fullscreen`.
