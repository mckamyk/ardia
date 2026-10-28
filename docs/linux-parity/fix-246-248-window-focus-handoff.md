# Fix handoff: keyboard focus in new windows, restore-behind focus and background windows (#246, #247, #248)

Branch `linux-window-focus`, based on `linux` at `a50dbac6`. Native only: `ac_linux_host.cc`, `ac_linux_window_lifecycle.{h,cc}` and a new Linux-only Chromium patch. No UI, shared or macOS code changed.

## #246: compositor focus without keyboard focus

**Root cause.** Views focus never reached the shell WebView. When a widget activates, `DesktopNativeWidgetAura::HandleActivationChanged` restores the focus manager's stored view; a new window has none, so it focuses the root view only. Chrome's `BrowserView` then runs `RestoreFocus()` on its first activation, which asks the active tab's `WebContents` to focus. On Linux that tab's contents view is hidden under Ardia's overlay (`LayoutOverlay` hides `GetActiveContentsWebView()`), and Aura won't focus a hidden window. So the window is active, the shell document has an `<input>` as `activeElement`, but `document.hasFocus()` stays false and keys reach no text client. The launch typeahead then discards what it held, because `ShellHostView::ShellHoldsTextField` requires `shell_->HasFocus()`.

**Probe on the `ee23b331` package** (headless sway and Xvfb + openbox, a scratch copy of the #240 harness): after launch the window has compositor focus and `document.hasFocus()` is false. Ctrl+L, whose accelerator runs `FocusShell()`, makes it true, and typed text then lands. That is the call this fix adds.

**Fix.** `ShellHostView::OnWidgetActivationChanged(active)` calls `FocusShellOnActivation()` first. It gives the shell WebView views focus (`FocusShell()`) only when either:
- nothing holds views focus, or
- the focused view is part of Chrome's own `BrowserView` UI outside the Ardia overlay (covered and unusable).

It leaves focus alone when:
- the focused view is inside the overlay (a page viewport, docked DevTools, the shell);
- the focused view belongs to another widget sharing the focus manager. In 156, tab-modal dialogs (alert, confirm, beforeunload, HTTP auth) are child widgets (`dialog_delegate.cc`, `params.child`), and `Widget::GetFocusManager()` returns the top-level widget's manager. When the window is activated again, `DesktopNativeWidgetAura` restores focus to the dialog's button before the observers run, so Enter keeps going to the dialog;
- the window has a window-modal transient child (`wm::GetModalTransient`).

Event order on activation:
1. Ozone reports activation (`wl_keyboard.enter`, X11 `FocusIn`/`_NET_ACTIVE_WINDOW`).
2. `DesktopNativeWidgetAura::HandleActivationChanged` restores the stored view, or focuses the root view.
3. Widget observers run in insertion order: `BrowserView` first (its `RestoreFocus` is a no-op here), then `ShellHostView`, which now focuses the shell.
4. `views::WebView::OnFocus` focuses the shell's `RenderWidgetHostView`; its text input client registers with the input method. If the shell's contents attach later, `WebView::AttachWebContentsNativeView` forwards the focus it already has.

Activation that arrives before the host attaches is replayed by `SeedWindowActivation()` at `Initialize`, so the startup window gets the same treatment. Settings and Import (utility windows) go through the same path.

**Not changed.** Keys typed before a Ctrl+N window has mapped still go to the old window: the compositor sends them there, as it does for stock Chromium.

## #247: a restored session focuses the last-mapped window

**Root cause.** Restoring a session opens the saved focused window first and the others with `{focus: false, behind: <focused>}` (`windowOpenOrder.ts`). The host shows those with `ShowInactive()`. On X11 that maps them with `_NET_WM_USER_TIME` 0 and the window manager leaves focus alone. Wayland has no inactive map: sway and Hyprland focus every newly mapped toplevel.

**Fix.** On Wayland only (`!GlobalScreenCoordinates()`, `OpenIntent::hand_back`), `FocusLedger::Create` records, for each regular window created inactive, which window should keep focus: the `behind` window when the renderer may focus it (`FocusReaches`), otherwise the window active at that moment. If the compositor activates the new window as it maps, `OnWidgetActivationChanged` asks `FocusLedger::Unrequested`, which returns the kept window once, and the host activates it again (`Show` + `Activate`, an `xdg_activation_v1` request).

Only the compositor's focus-on-map is undone, never the user's own activation:
- **Not on X11.** There `ShowInactive` maps with `_NET_WM_USER_TIME` 0 (`X11Window::Map`), so any activation is the user's.
- **First activation only.** That activation consumes the entry, whatever happens to it.
- **Around the first frame.** It must come no later than 300 ms (`kHandBackAfterFrameMs`) after the window's first presented frame. `ShellHostView` observes its compositor's `OnDidPresentCompositorFrame` and reports the first one (`Presented`). A wl_surface maps on its first buffer commit, and sway or Hyprland focus it right then; a person needs longer than that to see the window and switch to it. As a backstop, if no frame is ever reported, nothing is handed back later than 3 s (`kHandBackLimitMs`) after creation.
- **Explicit focus clears it.** Any explicit activation request clears every pending hand-back: `ArdiaShell.focusWindow`, a re-open asking for focus (`Reuse`), or the fullscreen restore's final focus.
- **A hand-back doesn't clear the others.** The hand-back's own activation goes through `FocusLedger::HandBack`, which requests activation without clearing. `windowSync` opens the saved window and then every other one behind it in one loop, so when a 3- or 4-window session restores, each behind window that the compositor focuses on map hands focus back in turn.
- **Pointer press.** Any mouse or touch press in an Ardia window after creation cancels it. With click-to-focus the keyboard enter can precede the button event; the first-frame bound covers that case.
- **Closing either window** removes it (`Forget`).
- **Not detected: a keyboard switch right after the map.** A keyboard switch (Super+N, Alt+Tab) to a behind window before its first frame is reported (up to the 3 s backstop), or within 300 ms after it, is still undone. On Wayland those bindings belong to the compositor: the client gets nothing before `wl_keyboard.enter`, which is the same event the compositor sends when it focuses a window on map. There's no cheap signal to tell them apart.
- **Background instances** never activate, so nothing is armed.

The new window still broadcasts its `focus` event, and the kept window's follows, so the store ends on the saved focused window. On Wayland the compositor decides whether to grant the activation: the harness's sway sets `focus_on_window_activation focus`; Hyprland's default `misc:focus_on_activate` is off, so there the kept window may only be marked urgent.

## #248: a background instance's window takes focus

**Root cause.** The host already shows its own windows inactive in a background instance. But Chrome shows the startup window itself (`StartupBrowserCreatorImpl` → `BrowserView::Show()`, an active map), and that happens before `PostBrowserStart` starts the Ardia host. The host had not even read `ARDIA_BACKGROUND` yet: `InitializeLinuxLaunchConfig` ran in `StartLinuxHost`.

**Fix.** `engine/patches/chromium-linux-background-windows.patch` (Linux, after `chromium-ardia-linux-layer.patch`):
- `ChromeMainDelegate::BasicStartupComplete` calls `ardiacore::InitializeLinuxLaunchConfig()`, so `--ardia-background` is set from `ARDIA_BACKGROUND=1` before any window exists. It is idempotent; `StartLinuxHost` still calls it.
- In a background instance, `BrowserView::Show()` maps a hidden window with `Widget::ShowInactive()` and leaves an already visible one alone. `ShowInactive` turns a saved maximized state into the restored bounds (`widget.cc`), so `Show()` reads the saved placement (`GetSavedWindowPlacement`, as the widget did at init) and calls `Maximize()` after the inactive show when it was maximized.
- In a background instance, `BrowserView::Activate()` still runs `BrowserActiveStateManager::DidBecomeActive()`, so the last-active browser stays right for `window.focus()`, `windows.update({focused: true})` and CDP `activateTarget`. Only the platform activation (`browser_widget_->Activate()`) is skipped.

The patch applies on both X11 and Wayland. It only matters on X11.

On X11 an inactive map sets `_NET_WM_USER_TIME` to 0 (`X11Window::Map`). openbox honours it: in F22X the restored behind window, which the host already shows inactive, never took focus. On Wayland a client can't refuse focus on map; sway and Hyprland will still focus the window. That is compositor policy, not something Ardia requests. The window stays mapped and visible, so the harness's screenshots of it still work.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `keyboard-focus`: activation (the fake restores the stored view first, as `DesktopNativeWidgetAura` does) focuses the shell when nothing or Chrome's covered UI holds focus; not when an overlay view does or a window-modal is up; seeded activation too | fail | pass |
| `tab-modal-dialog`: a dialog button restored on reactivation keeps focus | fail with the first version | pass |
| `restored-behind`: on Wayland a compositor activation at the first frame activates the kept window once; a pointer press cancels it; on X11 nothing is handed back | fail | pass |
| `many-behind`: a 3- and a 4-window restore (saved window plus 2 or 3 behind, opened in one loop); each behind window the compositor focuses hands focus back, so the saved window ends focused | fail before `HandBack` (only the first behind window handed back) | pass |
| `users-activation-stays`: an activation 1.7 s after the first frame, and one after `ArdiaShell.focusWindow`, are never undone | fail with the first version | pass |
| Ledger program (`Unrequested`, `Presented`, X11, first-frame bound, 3 s backstop, `Focus`/`Reuse` clearing, `HandBack` keeping the other entries in a 3- and 4-window restore); the existing `LEDGER` program had no runner, so `WindowLifecycleLedgerTests` now compiles and runs it | fail | pass |
| `test_background_windows.py`: series position, switch name, the patched `Show` (inactive, then `Maximize` after the weak-pointer check when saved maximized), `Activate` (`DidBecomeActive` kept, only the platform activation skipped) and `BasicStartupComplete` on the 156 sources with `patch -F0` | fail | pass |
| `scripts/linux/syntax-check-156.sh ac_linux_host.cc ac_linux_window_lifecycle.cc` | | pass |
| `chrome_main_delegate.cc` and `browser_view.cc` with the patch applied, clang `-fsyntax-only` with their `chrome_initial.ninja` and `ui.ninja` flags | | pass |

Putting back each reviewed behaviour makes a test fail:
- the old focus rule;
- arming on X11;
- `Focus` not clearing the hand-back;
- handing back through `Focus` instead of `HandBack`;
- no first-frame bound;
- removing the `FocusShellOnActivation()` or `ReturnUnrequestedActivation()` call;
- the old patch.

## Needs the next build

None of this has been compiled into a package. On the next Chromium 156 build, run F22D, F22X, W79D and W79X from the #240 harness (headless sway and Xvfb + openbox; the live-Hyprland variants prove nothing on the owner's session, where Hyprland keeps keyboard focus on the owner's terminal and even stock Chromium fails calibration):
- typing at launch, in Settings and after Ctrl+N (once the new window has mapped) lands;
- a restored two-window session focuses the saved window on sway;
- an `ARDIA_BACKGROUND=1` instance doesn't take focus on X11.
