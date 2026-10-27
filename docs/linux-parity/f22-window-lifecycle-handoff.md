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
| Existing or still-pending window, from another renderer | Activates when the intent asks for focus. Its `frame` is ignored: frames apply only at creation, as in `WindowManager.open` on macOS. Every renderer's first sync re-opens every window with its stored frame, and `BrowserView::SetBounds` exits fullscreen. |
| A window's own renderer asking for its own window | Never activates: it is the launch echo of `windowSync`'s first sync, and a reload would otherwise steal focus. Utility windows are the exception. |
| `focusWindow` | Always activates when allowed. |
| `focusWindow`, or Settings/Import reopened, in a background instance | `ShowInactive()` (shows or unminimizes without activating), as before this lane. |

**Activation mechanism.** Activation is `Show()` + `Activate()`. On Wayland, Chromium 156 turns this into an `xdg_activation_v1` request: it uses `XDG_ACTIVATION_TOKEN` if present, otherwise a token requested from the focused surface, and holds the token until the surface is configured. The compositor may still refuse.

**Readiness retry.** A pending activation is retried once when the window's renderer is ready, but only if no activation change was observed in any Ardia window since the request. "Ready" means one of two signals:

- its own-window `openWindow`, which `startNativeSync` sends right after `windows.listen()`;
- `ArdiaShell.utilityReady`.

This covers compositors that drop activation for unconfigured surfaces, without stealing focus after the user moved on. A mouse or touch press in any Ardia window between the request and readiness also cancels the retry. Key presses deliberately don't: typing right after Ctrl+N is exactly when the retry is wanted.

**Activation seeding.** A window already active when its host attaches (activation delivered before the posted `Attach` ran) is recorded in the ledger at `Initialize`, and its focus event is sent.

**Observed activation.** Each transition to active sends `onWindowEvent {type:"focus", id}` to every regular renderer. Utility windows send nothing.

**Frames.**

- Frames are `[x, y, width, height]` with a bottom-left origin at the primary display, y increasing upward. This is the macOS contract the shared drag code assumes.
- Reports are debounced for 250 ms and skipped while fullscreen or minimized, when repeated, and on platforms without global coordinates (Wayland).
- Incoming frames may be integers or doubles. A frame with a non-finite value, a width or height of zero or less, a width or height above 100000, or an origin beyond ±1e6 is ignored.
- On Wayland only the size is applied.

**Fullscreen.** `{type:"fullScreen", on}` is sent on widget show-state transitions.

**Replay.** When a renderer becomes ready, its own window's frame and fullscreen state are broadcast to every regular renderer, so windows the WM never moves are still known everywhere. The ready renderer also receives every other window's state, and the active window's focus arrives last.

**Wayland frames.** `ArdiaShell.windowFrame` replies `null`. No frame events are sent, and `openWindow` frames apply size only, so the store never holds invented positions.

**Page focus.** `{type:"pageFocus"}` is reported for the tab whose presented, visible page gains focus. The existing `HostWebView` mapping routes it to `onPageFocus` → `activateFocusedPane`. Focus the app requests itself (the webview `focus` command, `TabBinding::FocusFromHost`) is not reported, matching `host_focusing_` in `ac_tab_bridge.mm` and the macOS `page-focus` acceptance check.

## Tests run

`test_window_lifecycle.py` compiles the production host fragments against fakes and runs each scenario in its own process: the free activation helpers and `WindowPointerHandler`, the `windowFrame`/`focusWindow`/`utilityReady` and `openWindow` branches of `Invoke`, the widget observers, `RendererReady`/`SeedWindowActivation`/`WindowState`, `BroadcastWindowEvent`, and `TabBinding::FocusFromHost`/`OnWebContentsFocused`. One test feeds the production `windowFrame` replies into the real shared `state/drag/windowDrop.ts` through node. For every review finding, reverting just that fix in a scratch copy makes exactly its test fail; see the PR body for names.

Commands and counts are in the PR body.

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
