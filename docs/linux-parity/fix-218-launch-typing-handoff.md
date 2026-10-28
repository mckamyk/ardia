# Fix handoff: text typed into the command bar at launch stays there (#218)

Branch `t3/fix-launch-typing`, based on `linux` at `8f22774f`. Linux UI only (`apps/linux/src/shims`). No shared code and no native code changed.

## Root cause

The text wasn't cleared. It went into a different field.

Over the New Tab page the hero search field already has focus. Opening the command bar mounts a second field, and the shared `Omnibox` only focuses that field from its first `onLayout` (`onLayout={focusOnce}` in `BarField`). That idiom comes from React Native macOS, where the native view only exists after layout. On Linux, react-native-web reports `onLayout` from a `ResizeObserver` callback and then a `setTimeout(0)` measure. That's at least one rendered frame after the commit that opened the bar.

Right after launch the renderer is busy, so that frame comes late. Keys typed in the gap go into the hero field, which still has focus underneath the bar. When the bar's field finally focuses, it's empty. The hero field keeps the text, which is why a suggestion request went out for it. The store's panel actions never see it.

A probe on the `8f22774f` package shows the order:
1. `focusin` on the hero field (input 1).
2. `Input.insertText` puts "ardia typed at launch" into input 1.
3. 14 ms later, `focusout` and `focusin` move focus to the command bar's field (input 2), which is empty.

The same steps 5 s after launch, or over a loaded page (where there is no hero field), get the frame in before the first key.

## Fix

The Linux `TextInput` shim (`src/shims/react-native.tsx`) now reports a field's first layout from a layout effect, in the same commit that mounts it. It measures the way react-native-web's `UIManager.measure` does (`src/shims/web/mountLayout.ts`), but synchronously. So `onLayout={focusOnce}` fields (the command bar, the hero bar, Search Tabs) focus before any later input event is dispatched. react-native-web's own `ResizeObserver` still reports layouts after that. Every `onLayout` handler on a `TextInput` is a focus-once guard, so the second call does nothing.

macOS and the shared components are unchanged.

## Tests

| Test | Before | After |
| --- | --- | --- |
| W79 "text typed into the command bar right after launch stays in the field" (`escalate/launch-typing`, `98d3f2b1`), on the `8f22774f` package with this branch's UI | fail 2/2 (`""`) | pass 4/4 |
| `src/mountLayout.test.mjs` | fail (no module) | 4 pass |
| `test:host` | | 442 pass |
| `test:graph` | | 3 pass |
| `acceptance:self-test` | | 79 pass |
| Nearby scenarios F01, F09, F10, F18, W70, W77, W92, W114, E97, E98 | round 5: no failures | no failures; automated counts the same as round 5 (49 pass) |

These were run with `--ardia-ui-dir` against the packaged `8f22774f` build. That build has no native changes from this branch, so no Chromium rebuild is needed. The next package picks the fix up from `apps/linux/dist`.
