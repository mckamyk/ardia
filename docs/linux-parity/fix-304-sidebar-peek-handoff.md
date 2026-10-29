# #304: the left-edge sidebar peek keeps the page visible

Issue: [#304](https://github.com/mckamyk/ardia/issues/304), found while verifying Ctrl+S (#252) in a private sway session. Branch `fix3-drag-304`.

## Cause

With the sidebar hidden, a pointer at the window's left edge peeks the sidebar over the page's left edge. On Linux the page is a native view stacked above the shell, so `watchTabViewport` (`apps/linux/src/hostViewport.ts`) samples five points of the page area and hides the page when any of them hits a shell element other than the page or a known overlay. The peek panel covers the top-left and bottom-left samples, so the page was hidden for as long as the peek was open and the window showed only the shell background beside the sidebar. macOS keeps the page visible beside the peek.

## Fix (Linux UI only)

- `apps/linux/src/SidebarPeekMark.tsx` replaces `Sidebar` for `SidebarDock.tsx` only (an importer-scoped entry in `vite.config.ts`). While the window's peek is shown (`usePeekEdge`), it adds a pointer-transparent `ardia-sidebar-peek` view filling the peek panel, so the panel's rectangle (including its slide-in transform) is readable from the DOM. The docked sidebar is unchanged.
- `watchTabViewport` reports each `ardia-sidebar-peek` rectangle as an occlusion, like hover cards and the selection bar: the host cuts it (plus the 32 px shadow outset) out of the page's paint and input, the shell shows the page snapshot under the cut-out, and the rest of the page stays live. Sample points inside the peek rectangle count as covered by it.

The top-strip layout's peek is a different element and is not covered by this change.

## Tests

| Command | Result |
| --- | --- |
| `node --test src/hostViewport.test.mjs` | 4 passed; the new peek case failed before (the page was reported hidden) |
| `node --test scripts/acceptance/tests/sidebar.test.mjs` | 1 passed (the pixel verdict) |
| `pnpm --filter @ardia/linux test:graph` | passes; `SidebarDock.tsx` loads `SidebarPeekMark.tsx`, which alone loads the shared `Sidebar.tsx` |
| F25P on the 148104a3 package with its own UI | fails as reported: peek shown, 0 % of the page painted beside it |
| F25P on the 148104a3 package with this branch's UI | 3 passed |
