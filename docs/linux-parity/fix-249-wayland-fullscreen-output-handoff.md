# Fix handoff: a Wayland fullscreen window comes back on the output it was saved on (#249)

Branch `linux-wayland-output`, based on `linux` at `3930fa12`. Native only: `ac_linux_host.cc` and `ac_linux_window_lifecycle.{h,cc}`. No UI, shared or macOS code changed.

## Root cause

On Wayland a client can't read or set its global position, so the host sends no `frame` events and the store saves no position (`f22-window-lifecycle-handoff.md`). Fullscreen restore (`ArdiaShell.restoreFullScreen`, `ac_linux_fullscreen_restore`) calls `ToggleBrowserFullscreenMode`. That sends `xdg_toplevel.set_fullscreen` with no output, so the compositor puts the window on whichever output it maps on. In W77D that is HEADLESS-1, not the HEADLESS-2 it was saved on.

## Fix

**What is saved.** The host keeps the window's output label: the `wl_output` description Ozone puts in `display::Display::label()` (sway: `<make> <model> <serial> (HEADLESS-2)`). Wayland display ids are `wl_registry` names and change between runs; the label doesn't. The label lives in the app profile's Chrome window-placement pref, under `browser.app_window_placement.ArdiaLinuxWindowOutputs`, keyed by Ardia window id. The shared store and its session file are untouched.

**When it is saved.** Only on Wayland, for regular non-private windows, while the window is browser-fullscreen (`WindowFullScreen()`):
- on every geometry report (`ReportWindowGeometry`, which runs on show-state changes and 250 ms after bounds changes);
- when the widget closes (`OnWidgetClosing`), which covers quitting with Ctrl+Shift+Q.

**While a restore retargets the window.** `RestoreFullScreenNow` sets `output_target_` to the saved label. Until a report finds the window on a display with that label, nothing is saved, so the compositor's first choice never overwrites the saved output. The target is dropped once a report finds the window there, or when the window leaves fullscreen. If the compositor never moves the window, the saved label stays the one the user chose and the next launch tries it again.

**After the window departs.** `CommitWindowDeparture` removes the entry and marks the window forgotten (`output_forgotten_`). It runs before the widget closes, so without that mark `OnWidgetClosing` would save the entry again and `ArdiaLinuxWindowOutputs` would grow with every closed window.

**Not saved: moves between outputs of the same size.** A move to an output with the same size and scale produces no bounds change, so no geometry report runs until the next show-state change or the widget closes. Ozone tracks the output a surface entered (`wl_surface.enter`, `GetPreferredEnteredOutputId`) but tells the widget nothing that the host could observe cheaply. A crash after such a move restores to the previous output.

**Restore.** `RestoreFullScreenNow` reads the saved label before Chrome's toggle and matches it against `display::Screen::GetAllDisplays()` (`OutputLabelled`: exactly one display with that label). After `ToggleBrowserFullscreenMode`, it calls `Widget::SetFullscreen(true, <that display id>)`. On Wayland that reaches `xdg_toplevel.set_fullscreen(<wl_output>)` through `WaylandToplevelWindow::SetWindowState`. Ozone lets a fullscreen request with a target through even when already fullscreen. If the output is gone, or two outputs share the label, the window stays fullscreen wherever the compositor put it, as before.

Event order on relaunch, as 156 runs it (`AsyncFullscreenWindowState` is off by default):
1. The window maps on the compositor's output.
2. The renderer calls `restoreFullScreen`. `RestoreFullScreenNow` reads the saved label and sets `output_target_`.
3. `ToggleBrowserFullscreenMode` → `BrowserView::ProcessFullscreen` → `WaylandToplevelWindow::SetWindowState` changes the state synchronously. Inside the toggle, `OnWidgetShowStateChanged` runs:
   - its geometry report skips saving, because the window is not on the target yet;
   - `Entered` + `FinishFullScreenRestore` end a single-window restore right there.
4. `Widget::SetFullscreen(true, <saved display>)` sends `xdg_toplevel.set_fullscreen(<wl_output>)` and a second synchronous show-state notification. Its report still skips saving.
5. The compositor handles both requests in order; sway's fullscreen handler moves a container to the named output's workspace. W77D has to confirm this on a build.
6. A later report (a bounds change, or the close) finds the window on the target, clears the target and saves.

X11 is unchanged: its saved frame already places the window before fullscreen.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `test_window_lifecycle.py` `fullscreen-output`. The fakes deliver the show-state notification synchronously inside `ToggleBrowserFullscreenMode` and `Widget::SetFullscreen`, as 156 does. Checks: saved only while fullscreen; not for utility windows; forgotten on departure and not saved again by a later close; restore targets the saved display; neither synchronous report overwrites it; once on the target, saving resumes | fail (the first version saved the compositor's output and re-added departed windows) | pass |
| `fullscreen-output-gone`: a missing output falls back to the plain toggle | fail | pass |
| `x11-outputs`: nothing saved with global coordinates | fail | pass |
| `output-labels`: exactly one display must carry the label | fail | pass |
| Structural: saved on close, forgotten on departure | fail | pass |
| `scripts/linux/syntax-check-156.sh ac_linux_host.cc ac_linux_window_lifecycle.cc` | | pass |

Removing the retarget, the pending target, the forgotten mark or the recording call each makes a test fail.

## Needs the next build

Not compiled into a package yet. On the next build, run W77D (two-output headless sway). Also check by hand on Hyprland with two monitors that the compositor honours the output in `set_fullscreen`.
