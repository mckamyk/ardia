# #284: the selection popover is drawn over the page

Issue: [#284](https://github.com/mckamyk/ardia/issues/284), found by the desktop-session harness (#240, wave 2 pages lane, F16D). Branch `fix2-selection-popover`.

## Cause

The selection bar is a shell element in `PageOverlays`, positioned over the page. On Linux the page is a native view stacked above the shell, so the bar was laid out but never visible. Shell overlays reach the screen in one of two ways: modal ones (menus, popovers, prompts) hide the page through `setHostOverlayCheck`, and sidebar hover cards are reported as viewport occlusions, which the host cuts out of the page's paint and input shape while the shell shows a snapshot of the page underneath (`hostViewport.ts`, "Sidebar hover cards" in `docs/linux-port.md`). The selection bar was neither.

## Fix (Linux UI only)

- `apps/linux/src/SelectionPopover.tsx` replaces the shared component through the Linux build's component map and wraps it in a `ardia-page-overlay` view, as `SidebarHoverCard.tsx` does.
- `watchTabViewport` reports the bars inside `ardia-page-overlay` as occlusions, alongside hover cards and an open extension popup (#315): the page stays live except for each rectangle (plus the shadow outset), and the bar takes its own clicks.

Escape already works: the page script hides the selection on any non-modifier keydown and the page keeps its selection.

## Tests

| Command | Result |
| --- | --- |
| `node --test src/hostViewport.test.mjs` | 3 passed; fails before (the occluder query has no page-overlay selector); a selection bar and an open extension popup are cut out together |
| `pnpm --filter @ardia/linux test:graph` | passes; new row: the shared `SelectionPopover.tsx` is only loaded by its adapter |
| `pnpm --filter @ardia/linux typecheck`, `test:host`, `build` | pass |
| F16D on the 0f5d71dc package with this branch's UI (`run.mjs <ardia> <dist>`) | see the PR |
