# Fix handoff: keyboard focus in new windows, restore-behind focus and background windows (#246, #247, #248)

Branch `linux-window-focus`, based on `linux` at `8bcc3a82`. Native only: `ac_linux_host.cc`, `ac_linux_window_lifecycle.{h,cc}` and a new Linux-only Chromium patch. No UI, shared or macOS code changed.

## #246: compositor focus without keyboard focus

**Root cause.** Views focus never reached the shell WebView. When a widget activates, `DesktopNativeWidgetAura::HandleActivationChanged` restores the focus manager's stored view; a new window has none, so it focuses the root view only. Chrome's `BrowserView` then runs `RestoreFocus()` on its first activation, which asks the active tab's `WebContents` to focus. On Linux that tab's contents view is hidden under Ardia's overlay (`LayoutOverlay` hides `GetActiveContentsWebView()`), and Aura won't focus a hidden window. So the window is active, the shell document has an `<input>` as `activeElement`, but `document.hasFocus()` stays false and keys reach no text client. The launch typeahead then discards what it held, because `ShellHostView::ShellHoldsTextField` requires `shell_->HasFocus()`.

**Probe on the `ee23b331` package** (headless sway and Xvfb + openbox, a scratch copy of the #240 harness): after launch the window has compositor focus and `document.hasFocus()` is false. Ctrl+L, whose accelerator runs `FocusShell()`, makes it true, and typed text then lands. That is the call this fix adds.

**Fix.** `ShellHostView::OnWidgetActivationChanged(active)` calls `FocusShellOnActivation()` first. It gives the shell WebView views focus (`FocusShell()`) unless:
- the focus manager's focused view is already inside the Ardia overlay (a page viewport, docked DevTools or the shell itself), or
- the window has a modal transient child (a dialog keeps focus).

Event order on activation:
1. Ozone reports activation (`wl_keyboard.enter`, X11 `FocusIn`/`_NET_ACTIVE_WINDOW`).
2. `DesktopNativeWidgetAura::HandleActivationChanged` restores the stored view, or focuses the root view.
3. Widget observers run in insertion order: `BrowserView` first (its `RestoreFocus` is a no-op here), then `ShellHostView`, which now focuses the shell.
4. `views::WebView::OnFocus` focuses the shell's `RenderWidgetHostView`; its text input client registers with the input method. If the shell's contents attach later, `WebView::AttachWebContentsNativeView` forwards the focus it already has.

Activation that arrives before the host attaches is replayed by `SeedWindowActivation()` at `Initialize`, so the startup window gets the same treatment. Settings and Import (utility windows) go through the same path.

**Not changed.** Keys typed before a Ctrl+N window has mapped still go to the old window: the compositor sends them there, as it does for stock Chromium.

## #247: a restored session focuses the last-mapped window

**Root cause.** Restoring a session opens the saved focused window first and the others with `{focus: false, behind: <focused>}` (`windowOpenOrder.ts`). The host shows those with `ShowInactive()`. On X11 that maps them with `_NET_WM_USER_TIME` 0 and the window manager leaves focus alone. Wayland has no inactive map: sway and Hyprland focus every newly mapped toplevel.

**Fix.** `FocusLedger::Create` records, for each regular window created inactive, which window should keep focus: the `behind` window when the renderer may focus it (`FocusReaches`), otherwise the window active at that moment. If the compositor activates the new window anyway, `OnWidgetActivationChanged` asks `FocusLedger::Unrequested`, which returns the kept window once, and the host activates it again (`Show` + `Activate`, an `xdg_activation_v1` request on Wayland).

The hand-back is dropped when:
- any mouse or touch press happens in an Ardia window after creation (the user chose);
- 5 seconds pass after creation (`kUnrequestedActivationWindow`), so a later keyboard switch to that window is never undone;
- either window closes (`Forget`);
- the instance is a background one (it never activates).

The new window still broadcasts its `focus` event, and the kept window's follows, so the store ends on the saved focused window. On Wayland the compositor decides whether to grant the activation: the harness's sway sets `focus_on_window_activation focus`; Hyprland's default `misc:focus_on_activate` is off, so there the kept window may only be marked urgent.

## #248: a background instance's window takes focus

**Root cause.** The host already shows its own windows inactive in a background instance. But Chrome shows the startup window itself (`StartupBrowserCreatorImpl` → `BrowserView::Show()`, an active map), and that happens before `PostBrowserStart` starts the Ardia host. The host had not even read `ARDIA_BACKGROUND` yet: `InitializeLinuxLaunchConfig` ran in `StartLinuxHost`.

**Fix.** `engine/patches/chromium-linux-background-windows.patch` (Linux, after `chromium-ardia-linux-layer.patch`):
- `ChromeMainDelegate::BasicStartupComplete` calls `ardiacore::InitializeLinuxLaunchConfig()`, so `--ardia-background` is set from `ARDIA_BACKGROUND=1` before any window exists. It is idempotent; `StartLinuxHost` still calls it.
- In a background instance, `BrowserView::Show()` maps a hidden window with `Widget::ShowInactive()` and leaves an already visible one alone.
- In a background instance, `BrowserView::Activate()` does nothing.

On X11 an inactive map sets `_NET_WM_USER_TIME` to 0 (`X11Window::Map`). openbox honours it: in F22X the restored behind window, which the host already shows inactive, never took focus. On Wayland a client can't refuse focus on map; sway and Hyprland will still focus the window. That is compositor policy, not something Ardia requests. The window stays mapped and visible, so the harness's screenshots of it still work.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `test_window_lifecycle.py` `keyboard-focus`: activation focuses the shell; not when a page/overlay view holds focus or a modal is up; seeded activation too | fail | pass |
| `test_window_lifecycle.py` `restored-behind`: a compositor activation of a behind window activates the kept window once; a pointer press cancels it | fail | pass |
| `test_window_lifecycle.py` ledger program (`Unrequested`, `Settle`, `Forget`, background, nothing to keep); the existing `LEDGER` program had no runner, so `WindowLifecycleLedgerTests` now compiles and runs it | fail | pass |
| `test_background_windows.py`: series position, switch name, and the patched `Show`/`Activate`/`BasicStartupComplete` on the 156 sources with `patch -F0` | fail | pass |
| `scripts/linux/syntax-check-156.sh ac_linux_host.cc ac_linux_window_lifecycle.cc` | | pass |
| `chrome_main_delegate.cc` and `browser_view.cc` with the patch applied, clang `-fsyntax-only` with their `chrome_initial.ninja` and `ui.ninja` flags | | pass |

Removing the `FocusShellOnActivation()` call, the `ReturnUnrequestedActivation()` call or the patch file each makes its test fail.

## Needs the next build

None of this has been compiled into a package. On the next Chromium 156 build, run F22D, F22X, W79D and W79X from the #240 harness (headless sway and Xvfb + openbox; the live-Hyprland variants prove nothing on the owner's session, where Hyprland keeps keyboard focus on the owner's terminal and even stock Chromium fails calibration):
- typing at launch, in Settings and after Ctrl+N (once the new window has mapped) lands;
- a restored two-window session focuses the saved window on sway;
- an `ARDIA_BACKGROUND=1` instance doesn't take focus on X11.
