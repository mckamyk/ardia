# Fix handoff: W77D traced on package 148104a3 (#249)

Branch `linux-fullscreen-output`, based on `linux` at `148104a3`. Native only: `ac_linux_host.cc`.

## How it was traced

- A W77D variant (scratch harness, two-output headless sway 1.12, wlroots 0.20.2) prints `ArdiaLinuxWindowOutputs` from `Default/Preferences` after the first quit and after the relaunch.
- `WAYLAND_DEBUG=client` logged the browser's protocol traffic.
- The browser ran under `gdb -batch` through a wrapper that replaced `bin/ardia`. It used the unstripped `out/Release_GN_linux_x64/chrome` as the symbol file (same build id, `37ae7596…`). Breakpoints on the save, close and fullscreen paths printed backtraces and continued.

## sway 1.12, from its source

`sway/desktop/xdg_shell.c`, `handle_request_fullscreen`:
- returns early when the toplevel isn't mapped;
- otherwise, for a request with an output, moves the container to that output's active workspace, then calls `container_set_fullscreen`.

`view_map` (`sway/tree/view.c`) uses `toplevel->requested.fullscreen` and `requested.fullscreen_output` at map time, so a request sent before the map still picks the output.

So sway follows a targeted request. The traces confirm it: `set_fullscreen(wl_output#33)` put the window on HEADLESS-2.

PR #329's explanation (sway racing two requests) was wrong. The two failures below are Ardia's and Chrome's.

## Failure 1: the save kept the old output

HEADLESS-1's description is "Headless output 1" and HEADLESS-2's is "Headless output 3".

Without gdb, the first session saved "Headless output 1" (2 of 2 runs). The protocol log shows why:

| Time | Event |
| --- | --- |
| 00.925 | `move container to output HEADLESS-2`; the window gets a same-size configure |
| 00.945 | fullscreen configure; the save at this show-state change reads output 1, because the surface hasn't entered output 2 yet |
| 01.123 | `wl_surface.leave(HEADLESS-1)` / `enter(HEADLESS-2)`, about 200 ms after the move, once the surface commits there |
| 01.132 | quit keys |
| 01.219 | toplevel destroyed; no `unset_fullscreen` is ever sent |

The one-second re-check never ticks. The save at close does nothing: gdb shows `HostRegistry::OnBrowserClosed` → `ShellHostView::DetachBrowser` running before `OnWidgetClosing`, and `DetachBrowser` clears `browser_`, so `WindowOutputPrefs()` is null. The only `GetWindowPlacementDictionaryReadWrite` after it is Chrome's own `SaveWindowPlacement`.

**Fix.** `DetachBrowser` calls `RememberWindowOutput()` first, while the browser, the widget and the fullscreen state are still there. A window whose departure was already committed (closed by the user) stays forgotten and isn't saved.

## Failure 2: the relaunch left fullscreen 1.5 s later

With the right label saved (the gdb-slowed run, where a re-check tick landed before the quit):
- the relaunch sent `set_fullscreen(wl_output#33)` 0.9 s before the window mapped;
- sway put the window on HEADLESS-2;
- then Chrome itself sent `unset_fullscreen()`.

gdb traced the caller to `ExclusiveAccessManager::ExitExclusiveAccess` from a `OneShotTimer`. That is `ExclusiveAccessBubbleViews::OnPresentationTimeout` (`exclusive_access_bubble_views.cc`). The fullscreen exit bubble asks its popup's compositor for a successful presentation and exits fullscreen if none arrives within 1500 ms. Entered before the window was mapped, the bubble wasn't presented in time under software GL.

Going fullscreen on an already-mapped window, as the window manager's key does in the first session, keeps fullscreen. An earlier probe read it still fullscreen 1.5 s later.

**Fix.** The fullscreen restore waits for the window's first presented frame. `OnDidPresentCompositorFrame` sets `presented_` and calls `RestoreFullScreenNow()`, which returns early until then. A widget without a compositor counts as presented. The restore's own 10 s expiry still applies.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `test_window_lifecycle.py` `restore-after-first-frame`: no fullscreen request before the first presented frame (also not from `RendererReady`); one targeted request after it; none again on later frames | fail | pass |
| `test_window_lifecycle.py` structural: `DetachBrowser` saves the output before `RetireDocuments()` and before clearing `browser_` | fail | pass |
| `test_window_lifecycle.py` 34/34, `test_fullscreen_restore.py` 6/6 | | pass |
| `scripts/linux/syntax-check-156.sh ac_linux_host.cc` | | pass |

## Needs the next build

W77D on two-output headless sway. Not compiled into a package yet.

One case is still not covered: a quit that lands before the compositor's `wl_surface.enter` for the new output, under about 200 ms after a move, saves the old output. Nothing tells the client which output it's on before that event.
