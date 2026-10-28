# #280: Settings › Keyboard Shortcuts records Alt combinations

Issue: [#280](https://github.com/mckamyk/ardia/issues/280), found by the desktop-session harness (#240, E98D). Branch `fix/280-shortcut-recorder`.

## Cause

The recorder works. With the focus left in Search actions, Ctrl+Shift+K records at once: the runtime's window-level capture listener sees every key during recording, so the search field's caret doesn't matter. What it refused was every combination without Ctrl, Super or an F-key. It swallowed those keys (`preventDefault` and `stopImmediatePropagation`) and kept waiting, so Alt+Shift+U and Alt+Shift+K seemed to record nothing.

That rule was copied from macOS, whose recorder needs ⌘, Control or a function key and beeps otherwise. macOS leaves Option out because Option+key types characters. On Linux, Alt+key types nothing (AltGr, which does, doesn't set `altKey`). Chromium's own Linux accelerators and extension commands are built on Ctrl or Alt, and the acceptance step for #98 assigns Alt+Shift+U to an Ardia command. Refusing Alt meant Ardia could never take back an Alt-based extension shortcut.

## Fix (Linux UI only)

`apps/linux/src/shortcuts.ts`: `recordableShortcut(event)` takes a key with Ctrl, Super or Alt, or an F-key. Shift alone, AltGr characters, bare keys and modifier-only presses are still refused. `shortcutsRuntime.ts` uses it in place of the inline test. The native table already accepts Alt (`EF_ALT_DOWN`), and `OnKeyEvent` checks Ardia's table before extension keybindings, so an assigned Alt+Shift+U runs Ardia's command and never reaches the extension.

## Tests

| Command | Result |
| --- | --- |
| `node … --test src/shortcuts.test.mjs` | new test: Ctrl, Super, Alt and Alt+Shift combinations and F6 are recordable, but Shift+K, K, AltGr's `@` and modifier-only presses aren't, and a recorded Alt+Shift+U binds `duplicateTab` at keyCode 85, flags 10. Fails before the fix (no `recordableShortcut`). |
| `pnpm --filter @ardia/linux test:host` / `typecheck` | 534 passed / pass |
| Desktop session, ee23b331 engine, this UI: record Ctrl+Shift+K, then Alt+Shift+K, for Duplicate with the focus left in Search actions, then press each in the browser window | both record and both duplicate the tab. With the `linux` UI, Alt+Shift+K records nothing. The override reaches the browser window about 320 ms after recording (the session writer's 300 ms debounce). |
| E98D, ee23b331 engine | "assigning Alt+Shift+U to Duplicate … Reset All Shortcuts gives it back to the extension" passes with this UI and fails at recording with the `linux` UI. It needs one harness change: wait until the browser window has the new override (`settings.shortcuts.duplicateTab`) before pressing the key. Without the wait, the scenario presses within that ~320 ms and the extension still owns the key. |

`src/shortcutsRuntime.test.mjs` isn't run by any script, and on `linux` it fails 3 of 4 under the host loader and doesn't load under the input loader, so it is left as is.
