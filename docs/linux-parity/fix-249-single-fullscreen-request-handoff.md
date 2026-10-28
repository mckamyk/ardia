# Fix handoff: one targeted fullscreen request when restoring onto a saved output (#249)

Branch `linux-window-followups`, based on `linux` at `a896721d`. Native only: `ShellHostView::RestoreFullScreenNow` in `ac_linux_host.cc`.

## What the candidate build showed

Package `cand-980e7f14` (linux `6685c834` + #314 + #293 + #313 + #312) still failed W77D. A W77D variant that reads `Default/Preferences` right after the first quit (scratch harness, headless two-output sway) showed:
- The save is right now: `ArdiaLinuxWindowOutputs.main` is "Headless output 3", sway's description for HEADLESS-2 in that session. #314's one-second re-check did its job.
- The relaunch is wrong, but not the same way each time. In this run the window came back on HEADLESS-2 but not fullscreen. In the coordinator's run it came back fullscreen but on HEADLESS-1.

## Cause

On a saved output, the restore sent two fullscreen requests back to back:
1. Chrome's `ToggleBrowserFullscreenMode`, an untargeted `xdg_toplevel.set_fullscreen(NULL)`;
2. the targeted `Widget::SetFullscreen(true, <saved display>)`, `set_fullscreen(<wl_output>)`.

sway's fullscreen request handler, as I recall its source (not re-read), does two things:
- a request with an output first moves the container to that output's workspace, then calls `container_set_fullscreen`;
- `container_set_fullscreen` returns early when the container is already in that fullscreen mode.

So the second request can move an already-fullscreen container and leave it windowed on HEADLESS-2. Or, depending on when each request lands, the first request's fullscreen on HEADLESS-1 wins. Two different wrong outcomes from the same code fit a race between the two requests.

## Fix

When a saved output matches exactly one display, the restore sends only the targeted request, `Widget::SetFullscreen(true, <display>)`. Otherwise it toggles as before.

A fullscreen change that doesn't come from `FullscreenController` takes the same path as the window manager's fullscreen key, the way W77D makes the window fullscreen in the first place:
- `BrowserDesktopWindowTreeHostLinux::OnWindowStateChanged` → `BrowserView::FullscreenStateChanging` / `FullscreenStateChanged`;
- `FullscreenController::IsFullscreenForBrowser()` reports it as browser fullscreen, so F11 exits it.

The pending output target and the one-second re-check from #314 are unchanged.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `test_window_lifecycle.py` `fullscreen-output`: with a saved output the restore sends one targeted request and no toggle (`Toggles == 0`, targets `{20}`, fullscreen) | fail | pass |
| `fullscreen-output-gone`: without a matching output the restore still toggles | pass | pass |
| `test_window_lifecycle.py` 32/32, `test_fullscreen_restore.py` 6/6 | | pass |
| `scripts/linux/syntax-check-156.sh ac_linux_host.cc` | | pass |

## Needs the next build

W77D on a two-output headless sway. Not compiled into a package yet.

## F02D on the candidate (#12)

F02D's failure in the candidate batch run didn't reproduce as a regression of #314:

| Package | Run alone | With W79D, F22D and F22X running at the same time |
| --- | --- | --- |
| candidate, bundled UI | pass | fail: Settings' Privacy pane and the profile menu time out (10 s, 8 s) |
| candidate, UI with the expected-field wait disabled | pass | not run |
| `0f5d71dc` (before #314) | pass | fail: the isolated profile's site doesn't show within 10 s |

The batch run's failure cascaded from its first check. Settings' renderer never appeared within 20 s, the next `Ctrl+W` then closed the isolated window's tab, and the later "no document focus after two clicks" followed from that.

A probe of the isolated window's renderer showed its active tab with its URL from 463 ms on. So it doesn't wait for a field, and keeps the 500 ms readiness rule.

F02D's early checks are load-sensitive before and after #314. That points at the harness's waits (`d-profiles.mjs`), not a product change.
