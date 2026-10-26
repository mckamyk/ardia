# Linux everyday page commands and application dialogs

Draft planning layer 2/6. This workstream describes repairs owned by [#11 F01](https://github.com/mckamyk/ardia/issues/11), [#15 F05](https://github.com/mckamyk/ardia/issues/15), [#33 F23](https://github.com/mckamyk/ardia/issues/33). It adds no application behavior and closes no repair issue. The Git base chain is document order; use the [coordination tracker](https://github.com/mckamyk/ardia/issues/10) and [index](README.md) for actual blockers and parallel lanes.

## Integration contract

Suggested role: **Native page-command and dialog owners**. File boundary: Separate Linux page-command and dialog helpers; coordinate narrow dispatch registration in ac_linux_host.cc. Current find/commands state consumers are references, not a reason to fork shared UI.

- Owned per-tab command arguments and cancellation.
- Window-owned confirm/prompt results.
- Find and zoom event state.

F01 page commands and F05 application dialogs are independent. Chromium print/save dialogs are not the application confirm/prompt backend. F23 history/zoom can share the page observer owner without waiting for the entire event workstream. Preserve consumed/remapped shortcut behavior and modal ownership.

## F01 Find, Print, Save Page and DevTools are exposed but cannot execute

[#11 F01](https://github.com/mckamyk/ardia/issues/11) · P1 · persists

Ctrl+F opens a search bar that cannot search a native page. Print, system print, Save Page, DevTools, Inspect and Console actions reject instead of completing.

### Current source evidence

Shared FindBar still calls find/stopFinding and command dispatch calls print/runPageCommand/showDevTools. Linux HostWebView forwards them as tab.command, whose native dispatcher still lacks these methods. Linux shortcuts consume matched accelerators, preventing normal Chromium shortcut fallback. Domain migration does not supply per-tab commands.

- [apps/browser/src/components/find/FindBar.tsx:48](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/components/find/FindBar.tsx#L48)
- [apps/browser/src/state/commands/commands.ts:168](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/state/commands/commands.ts#L168)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1434](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1434)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1045](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1045)

### Implementation slices

1. Implement typed per-tab find, print/system-print/save-page and DevTools modes using Chromium helpers.
2. Emit find results and preserve tab ownership, cancellation and native modal shortcut ownership.

### Acceptance

- [ ] Find known text with counts, previous/next and dismissal on two real tabs.
- [ ] Print/save and all DevTools modes produce actual correct-tab results from shell and page focus.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#15 F05](https://github.com/mckamyk/ardia/issues/15), [#27 F17](https://github.com/mckamyk/ardia/issues/27), [#33 F23](https://github.com/mckamyk/ardia/issues/33), [#44 R01](https://github.com/mckamyk/ardia/issues/44). These are not additional whole-issue blockers.

Scope: Source revalidated after rebase; current UI cannot be runtime-tested until N01/N02/N03/N04 bootstrap compatibility is repaired. Chromium context-menu alternatives may exist.

## F05 Application confirm/prompt dialogs have no Linux implementation

[#15 F05](https://github.com/mckamyk/ardia/issues/15) · P2 · persists

Essential-tile rename, bookmark-folder creation, pinned-URL editing, profile deletion and warning-enabled group/last-tab closes can fail at the awaited dialog.

### Current source evidence

Shared callers moved into state and now import platform/dialogs. That platform module still reaches the Linux shell override, which sends unsupported ArdiaShell.confirm/prompt operations. Awaited destructive/rename actions therefore remain blocked.

- [apps/linux/src/shims/shellBase.tsx:74](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/linux/src/shims/shellBase.tsx#L74)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2074](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2074)
- [apps/browser/src/state/sidebar/actions.ts:45](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/state/sidebar/actions.ts#L45)
- [apps/browser/src/state/sidebar/actions.ts:125](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/state/sidebar/actions.ts#L125)
- [apps/browser/src/state/browser/actions.ts:47](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/state/browser/actions.ts#L47)
- [apps/browser/src/state/browser/actions.ts:65](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/state/browser/actions.ts#L65)
- [apps/browser/src/state/browser/actions.ts:95](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/state/browser/actions.ts#L95)

### Implementation slices

1. Implement requester-owned native Linux confirm/prompt helpers with suppression, keyboard semantics and promise cancellation.
2. Serialize dialog requests and respect native modal ownership.
3. Keep mutations after a successful result and settle requests when renderer/window disappears.

### Acceptance

- [ ] Rename an essential, create a bookmark folder and edit its URL.
- [ ] Cancel/confirm group, last-tab, profile and credential deletion dialogs.
- [ ] Suppression persists; Escape/Enter work; closing owner leaves no orphan dialog.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

Blocks full acceptance of: [#31 F21](https://github.com/mckamyk/ardia/issues/31).

Coordinate contracts/shared files with [#31 F21](https://github.com/mckamyk/ardia/issues/31), [#16 F06](https://github.com/mckamyk/ardia/issues/16), [#17 F07](https://github.com/mckamyk/ardia/issues/17). These are not additional whole-issue blockers.

Scope: Inline row rename is separate; website JS dialogs are Chromium-owned. Last-tab confirmation depends on effective last-tab policy. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## F23 History jumps and zoom feedback only partially work

[#33 F23](https://github.com/mckamyk/ardia/issues/33) · P2 · persists

Selecting an older history entry fails when it requires a multi-step jump. Page zoom changes natively, while Ardia’s percentage and nondefault-zoom indicator can remain at 100%.

### Current source evidence

Native navigationEntries and actual zoomStep still exist; goToOffset does not. Shared history selection uses normal back/forward for +/-1 but goToOffset for longer jumps. No native zoom event updates shared tab.zoom. New domain sites.zoomLevels/setZoom additionally need N02 mapping.

- [apps/browser/src/state/tabs/navigation.ts:123](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/state/tabs/navigation.ts#L123)
- [apps/browser/src/components/window/TabWebView.tsx:187](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/components/window/TabWebView.tsx#L187)
- [apps/browser/src/components/site/ZoomControls.tsx:11](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/apps/browser/src/components/site/ZoomControls.tsx#L11)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1406](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1406)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1434](https://github.com/mckamyk/ardia/blob/b70711df2af16cee08146b78f7d3405c57d6d1de/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1434)

### Implementation slices

1. Implement validated native history offset jumps and zoom observer/initial report.
2. Map saved per-site zoom via current sites domain contract.

### Acceptance

- [ ] Four-page history supports jumps back/forward by two or more.
- [ ] Native page scale, displayed percentage, nondefault indicator and reset agree.
- [ ] Per-site zoom persists and resets without stale navigation feedback.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#16 F06](https://github.com/mckamyk/ardia/issues/16), [#27 F17](https://github.com/mckamyk/ardia/issues/27), [#11 F01](https://github.com/mckamyk/ardia/issues/11). These are not additional whole-issue blockers.

Scope: Basic native back/forward/actual zoom code remains present. Current UI runtime still blocked by bootstrap/identity migration.

## Verification for implementation work

Use the current source baseline documented in the index. After N01–N05 restore runnable current-build acceptance, run Linux typecheck/build, focused domain and host fixtures, and actual native feature scenarios. Use physical compositor input for focus/drag/window claims and controlled service or scratch-profile fixtures for network, secrets, import and sync. Preserve native Chromium fallback behavior while testing Ardia’s own user controls.

Do not clean existing Chromium outputs or touch owner profiles. Preserve macOS/demo behavior, current source/API conventions and the no-comments rule for code. This planning document itself has no application tests to pass; a later implementation must record commands, results, current source/binary/UI identities and remaining acceptance requirements in its PR.
