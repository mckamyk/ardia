# Fix handoff: a popup opened right after pinning anchored off screen (#279)

Linux UI only: the `View` adapter in `apps/linux/src/shims/web/motion/macView.tsx`.

## Cause

Not the popup's size. A popup's anchor is read once, when it opens, from the registry the toolbar buttons fill from `onLayout`. react-native-web delivers `onLayout` from a `ResizeObserver`, which only runs in a rendering step. Pinning an extension unmounts the empty `*` anchor and mounts the button; the shell then produced no animation frame for about 600 ms (probe on the `0f5d71dc` package under headless sway: the button was in the DOM 6 ms after pinning, its `ResizeObserver` fired at 609 ms, the first frame after that). E98D pins the follow-up fixture and presses Alt+Shift+Y within that window, so neither anchor was registered and the popup took the `x: 100000` fallback for good. With the anchor present, the follow-up popup (no fixed size) sizes to its content as Chrome's does (57×83 for its one paragraph).

## Fix

The Linux `View` reports its first layout from a layout effect, measured like react-native-web's `UIManager.measure` (`reportMountLayout`, the same helper the Linux `TextInput` already uses for #218). A toolbar button's anchor is registered in the commit that mounts it. Later layouts still come from the `ResizeObserver`.

## Tests

- `mountLayout.test.mjs`: the Linux `View` reports its first layout from a layout effect (failed before).
- `pnpm --filter @ardia/linux test:host` 537/537, `typecheck`, `test:graph`.
- **End to end** (headless sway, `0f5d71dc` package, this branch's UI): E98D's first check, Alt+Shift+Y opening the popup anchored under the button for this window, passes; it failed on the stock UI with `anchor.x: 100000`.
