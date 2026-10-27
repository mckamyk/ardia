# F22 window lifecycle handoff

Lane F, branch `t3/linux-window-lifecycle`, based on `linux` at `4b3d21e0`. This lane closes [#32 F22](https://github.com/mckamyk/ardia/issues/32) at source level. It also lands the source-level fixes for [#44 R01](https://github.com/mckamyk/ardia/issues/44), [#45 R02](https://github.com/mckamyk/ardia/issues/45) (X11 coordinates) and [#46 R03](https://github.com/mckamyk/ardia/issues/46). Those three are recorded in [desktop-investigations.md](desktop-investigations.md).

## What changed

- `engine/ardiacore-linux/src/ardia/linux/ac_linux_window_lifecycle.{h,cc}`: a helper using only the standard library, with three parts:
  - `FocusLedger`: requested focus versus observed activation.
  - `GeometryLedger`: frame and fullscreen reports.
  - `SharedFrame` / `RequestedFrame`: coordinate conversion.
- `ac_linux_host.cc`: narrow hunks covering:
  - activation helpers that replace `FocusNativeWindow`;
  - intent and frame parsing in `ArdiaShell.openWindow`;
  - `ArdiaShell.windowFrame` and `ArdiaShell.focusWindow`;
  - the `utilityReady` readiness hook;
  - `ShellHostView` widget observers: activation, bounds, show state, readiness replay;
  - `TabBinding::OnWebContentsFocused`;
  - `OnBrowserClosed` cleanup and destructor cleanup.
- `BUILD.gn` adds the helper and the `//ui/display` and `//ui/ozone` dependencies.

## Contract

**Open intent.** `openWindow(id, {focus, behind, frame, kind})`:

- `focus` defaults to `true` (as `ShellModule.swift` does).
- A non-empty `behind` forces an inactive show.
- Settings and Import always ask for activation.

**Activation policy.** Background instances (`ARDIA_BACKGROUND=1`, `--headless`, `--ozone-platform=headless`) never activate.

| Request | Result |
| --- | --- |
| New window | Shown active only when focus is requested and allowed, otherwise `ShowInactive`. |
| Existing or still-pending window, from another renderer | Activates when the intent asks for focus. |
| A window's own renderer asking for its own window | Never activates: it is the launch echo of `windowSync`'s first sync, and a reload would otherwise steal focus. Utility windows are the exception. |
| `focusWindow` | Always activates when allowed. |

**Activation mechanism.** Activation is `Show()` + `Activate()`. On Wayland, Chromium 156 turns this into an `xdg_activation_v1` request: it uses `XDG_ACTIVATION_TOKEN` if present, otherwise a token requested from the focused surface, and holds the token until the surface is configured. The compositor may still refuse.

**Readiness retry.** A pending activation is retried once when the window's renderer is ready, but only if no activation change was observed in any Ardia window since the request. "Ready" means one of two signals:

- its own-window `openWindow`, which `startNativeSync` sends right after `windows.listen()`;
- `ArdiaShell.utilityReady`.

This covers compositors that drop activation for unconfigured surfaces, without stealing focus after the user moved on.

**Observed activation.** Each transition to active sends `onWindowEvent {type:"focus", id}` to every regular renderer. Utility windows send nothing.

**Frames.**

- Frames are `[x, y, width, height]` with a bottom-left origin at the primary display, y increasing upward. This is the macOS contract the shared drag code assumes.
- Reports are debounced for 250 ms and skipped while fullscreen or minimized, when repeated, and on platforms without global coordinates (Wayland).
- Incoming frames may be integers or doubles. A frame with a non-finite value, a width or height of zero or less, a width or height above 100000, or an origin beyond ±1e6 is ignored.
- On Wayland only the size is applied.

**Fullscreen.** `{type:"fullScreen", on}` is sent on widget show-state transitions.

**Replay.** When a renderer becomes ready, it receives every regular window's current frame and fullscreen state, then the active window's focus.

**Page focus.** `{type:"pageFocus"}` is reported for the tab whose presented, visible page gains focus. The existing `HostWebView` mapping routes it to `onPageFocus` → `activateFocusedPane`.

## Tests run

All runs were on this branch, except where marked as `linux`.

- `python3 -m unittest engine/ardiacore-linux/tests/test_window_lifecycle.py`: 11 passed. Before the change it failed with 9 failures and 2 errors.
- `python3 -m unittest discover -s engine/ardiacore-linux/tests`: see the PR body for counts. Failures that also occur on a pristine `linux` export are pre-existing and listed there.
- `pnpm --filter @ardia/linux test:host`, including the new `src/windowLifecycle.test.mjs` (6 tests): see the PR body.
- `pnpm --filter @ardia/linux typecheck`: passed.
- `pnpm --filter @ardia/linux build`: passed.
- `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`: see the PR body.

## Needs native runtime acceptance (not done)

I could not compile or run Chromium 156 or provide physical input. The host hunks use only Chromium APIs checked against the 156 sources:

- `views::WidgetObserver::OnWidgetActivationChanged`, `OnWidgetBoundsChanged` and `OnWidgetShowStateChanged`;
- `Widget::IsFullscreen`, `IsMinimized` and `GetWindowBoundsInScreen`;
- `display::Screen::Get()->GetPrimaryDisplay()`;
- `ui::OzonePlatform::IsInitialized` and `GetPlatformProperties().supports_global_screen_coordinates`;
- `content::WebContentsObserver::OnWebContentsFocused`;
- `base::OneShotTimer`.

None of this has been compiled. It still needs:

1. An incremental Chromium 156 build of `//ardia/linux:linux`.
2. On Hyprland and on X11, Ctrl+N followed by immediate typing lands in the new window.
3. Restore-behind and `{focus:false}` windows never activate.
4. Settings and Import still come to the front.
5. `ARDIA_BACKGROUND=1` instances never activate.
6. The R01, R02 and R03 runtime checks in [desktop-investigations.md](desktop-investigations.md).
