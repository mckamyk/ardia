# Fix handoff: replayed keys on X11, keys typed into a new window, and the fullscreen output on Wayland (#297, #246, #79, #249)

Branch `linux-window-input`, based on `linux` at `0f5d71dc`. The harness run on package `0f5d71dc` (`~/.cache/acceptance-0f5d71dc/r1`) found four problems:
- F22X and W79X crash on X11;
- W79D turns "abc" typed into a new Ctrl+N window into "bca", and a dead key typed there lands in the first window;
- F22D's "abc" is lost;
- W77D brings the fullscreen window back on HEADLESS-1.

## #297: SIGSEGV replaying held keys on X11

**Cause.** `ShellHostView::ReplayTypeahead` hands copies of the held key events to the window's input method. `ui::Event`'s copy constructor doesn't copy `target_`, so the copies have no target. On X11 the input method context is GTK3's (`GtkUiPlatformX11::CreateInputMethodContext`), and `InputMethodContextImplGtk::DispatchKeyEvent` casts `key_event.target()` to `aura::Window*` and calls `GetBoundsInScreen()` on it (`input_method_context_impl_gtk.cc:220`).

**Fix.** Each replayed event now gets the widget's native window as its target (`ui::Event::DispatcherApi::set_target`) before `InputMethod::DispatchKeyEvent`. That window has a host, so `GetTargetWindow` also finds the GdkWindow.

On Wayland the context is `WaylandInputMethodContext` (GTK returns none there). It reads only the key and its IME-ignored property, which the copy keeps, so Wayland never crashed. It now gets the target too.

## #246, #79: keys typed into a new Ctrl+N window were lost or reordered

**Cause.** The held keys are replayed when the Linux UI announces `ArdiaShell.typeaheadReady(true)`, and discarded on `false`. The UI said `false` when no text field had focus 500 ms after the window's first React commit.

A probe on `0f5d71dc` (headless sway, CDP listeners in the new window from the moment its target appears) showed the timing:
- The new-tab page's Omnibox focuses its field 1.0–1.2 s after the window takes compositor focus, from its own `onLayout` → `focusOnce` → `setSelectionRange(0,0)`. That's after the 500 ms deadline, so the keys were discarded (F22D, and three of four probe runs).
- In the fourth run, `a` was replayed 9 ms after the field focused. The renderer then stayed busy for 1.3 s, and by the time `a`-up, `b` and `c` were handled the caret had been reset to 0, giving "bca".

**Fix** (Linux UI only, `apps/linux/src/typeahead.ts`):
- **Expecting a field.** A window whose active tab is a new tab, or that has no active tab yet (`windowExpectsField`), keeps waiting for a field for up to 8 s, under the host's 10 s deadline. A window showing a page still gives up after 500 ms, so typing into a restored page window isn't held longer than before.
- **Idle before announcing.** Once a field holds focus, readiness waits for the renderer to be idle (`requestIdleCallback`, 2 s cap) and checks the field again, so the replay doesn't land while the window is still booting.

**Verified on the `0f5d71dc` package with this UI** (`--ardia-ui-dir`), with no native change needed:
- W79D passes 6/6, including typing as soon as the compositor focuses the Ctrl+N window and a dead key there.
- F22D passes all automated checks.

The dead key landing in the first window didn't reproduce in the probe and passes with this UI. Its cause wasn't isolated beyond the same readiness timing.

## #249: the fullscreen output saved on Wayland was the old one

**Cause.** A probe on `0f5d71dc` (two-output sway, equal scales) showed Ozone reporting the output change late:
- After a same-size move to HEADLESS-2, `GetDisplayNearestWindow` still gave output 1 1.5 s later, and switched only once going fullscreen changed the bounds.
- The save at the fullscreen transition could read the old output, and nothing read it again.

W77D's profile held "Headless output 1" right after the first quit.

With HEADLESS-2 at scale 2 the switch is immediate (Ozone prefers the higher-scale entered output), and moving back clears it. So enter and leave are delivered; the equal-scale case is the late one.

**Fix.** While a Wayland window is browser-fullscreen, `RememberWindowOutput` keeps a one-second `RepeatingTimer` that reads the output again and saves only when it changed. Leaving fullscreen, departing the session or closing stops the timer. This also covers the earlier gap: moves between same-size outputs are now saved without a bounds change.

**Restore path checked on `0f5d71dc`.** In a W77D variant that toggles fullscreen 2 s after the move, the correct label ("Headless output 3", sway's description for HEADLESS-2 in that session) was saved. The relaunch then came back fullscreen on HEADLESS-2. So the targeted `set_fullscreen(output)` works; only the save was wrong.

## #247: the fullscreen restore's final focus dropped the behind windows' hand-backs

Once the window really comes back on HEADLESS-2, the restored behind window maps on HEADLESS-1 and sway focuses it. The fullscreen restore's final focus went through `FocusLedger::Focus`, which clears every pending hand-back, so focus stayed on the behind window. In the W77D variant, "the window that had focus has it again" failed.

The final focus restores the session's own saved focus, the same target as those hand-backs. It now uses `FocusLedger::HandBack`, which doesn't clear them. Explicit requests (`focusWindow`, a focusing re-open) still clear.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `test_typeahead.py` `TypeaheadReplayTests`: the real `ReplayTypeahead` with 156-shaped fakes. The `Event` copy drops the target, and the fake GTK-style input method casts the target and reads its bounds, under ASan/UBSan | fail (member call on null `aura::Window`) | pass |
| `apps/linux/src/typeahead.test.mjs`: waiting for a late field in a new-tab window, idle before announcing, re-check when focus moves away, 8 s cap, page windows keep 500 ms, `windowExpectsField` | fail | pass (10) |
| `test_window_lifecycle.py` `late-output`: a fullscreen window's output change is saved by the watch without a report; leaving fullscreen stops it | fail | pass |
| `test_window_lifecycle.py` `restore-keeps-hand-back`: after the restore's final focus, the behind window's compositor activation still hands focus back | fail | pass |
| W79D, F22D on `0f5d71dc` with this UI (two runs each) | fail | pass |

## Needs the next build

Native, not compiled into a package yet:
- the X11 replay target (#297: F22X, W79X);
- the output watch (#249: W77D);
- the restore's final focus (#247: W77D "the window that had focus has it again").

These are verified only by unit tests and `scripts/linux/syntax-check-156.sh`. The UI change is verified on `0f5d71dc`.
