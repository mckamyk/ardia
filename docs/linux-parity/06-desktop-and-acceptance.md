# Linux desktop lifecycle and native acceptance scenarios

Draft planning layer 6/6. This workstream describes repairs owned by [#31 F21](https://github.com/mckamyk/ardia/issues/31), [#32 F22](https://github.com/mckamyk/ardia/issues/32), [#44 R01](https://github.com/mckamyk/ardia/issues/44), [#45 R02](https://github.com/mckamyk/ardia/issues/45), [#46 R03](https://github.com/mckamyk/ardia/issues/46). It adds no application behavior and closes no repair issue. The Git base chain is document order; use the [coordination tracker](https://github.com/mckamyk/ardia/issues/10) and [index](README.md) for actual blockers and parallel lanes.

## Integration contract

Suggested role: **Native window lifecycle and desktop acceptance owners**. File boundary: One owner for close/focus/geometry integration in ac_linux_host.cc; a separate drag owner consumes agreed window identities. Acceptance authors use scratch profiles and keep native source ownership separate.

- Regular-window close request/acknowledgment.
- Requested focus versus observed activation.
- Compositor-appropriate geometry and native drag target identity.
- Feature-level current-build acceptance evidence.

F21 native close interception can start before F05 dialogs land, but warning acceptance waits for them. F22 requested foreground focus and R03 observed activation/placement share one native window owner. Investigate R01/R02/R03 with physical native input; do not mark source hypotheses fixed merely by adding DOM tests. Standard Wayland does not guarantee usable global window coordinates, so drag targeting must use supported native/compositor contracts.

This workstream owns the final acceptance matrix across every issue in the program; the index and coordinator enumerate all 36 findings. It does not defer all other verification until this layer: each implementation needs its own focused evidence.

## F21 The window close button bypasses the multi-tab warning

[#31 F21](https://github.com/mckamyk/ardia/issues/31) · P2 · persists

A user with “Warn before closing a window with multiple tabs” enabled can close the native browser window without the Ardia confirmation.

### Current source evidence

Header non-utility close still forwards directly to native closure without closeRequest. Shared guard moved to state/windows and now finishes via closeWindowAnimated, but Linux emits no closeRequest. Only utility close interception exists.

- [apps/linux/src/TrafficLights.tsx:21](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/TrafficLights.tsx#L21)
- [apps/linux/src/utilityClose.ts:6](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/utilityClose.ts#L6)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1894](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1894)
- [apps/browser/src/state/windows/windowClose.ts:13](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/windows/windowClose.ts#L13)
- [apps/browser/src/state/windows/windowClose.ts:31](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/windows/windowClose.ts#L31)
- [apps/browser/src/state/settings/settings.ts:114](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/settings/settings.ts#L114)

### Implementation slices

1. Unify header/compositor/shortcut close into request/approve/cancel protocol.
2. Implement F05 warning first for final integration, then flush canonical writes before native close.
3. Provide Linux-safe completion fallback for animateWindowClose and preserve beforeunload/renderer-failure behavior.

### Acceptance

- [ ] Two tabs with warning on/off across header, compositor and window-close shortcuts.
- [ ] Cancel retains native window/tabs; repeated requests yield one dialog.
- [ ] Suppression, save failures and beforeunload cancellation leave no stuck pending close.

### Dependencies and parallel handoff

Blocked for full acceptance by: [#15 F05](https://github.com/mckamyk/ardia/issues/15).

No issue-wide downstream block is declared.

- The multi-tab warning requires a functioning owned confirm dialog; native close interception can be implemented in parallel.

Coordinate contracts/shared files with [#15 F05](https://github.com/mckamyk/ardia/issues/15), [#14 F04](https://github.com/mckamyk/ardia/issues/14), [#46 R03](https://github.com/mckamyk/ardia/issues/46). These are not additional whole-issue blockers.

Scope: Chromium beforeunload remains separate. New unconditional animateWindowClose introduces an additional synchronous failure if the guard becomes reachable; do not count a missing animation as missing warning itself. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## F22 New regular windows ignore their requested foreground focus

[#32 F22](https://github.com/mckamyk/ardia/issues/32) · P2 · persists

Ctrl+N and other new-window flows may leave the new window inactive while keyboard input stays in the previous window.

### Current source evidence

Shared window creation now uses a WindowPort but still passes focus/behind. Linux openWindow still never reads focus; utilities focus and regular windows always ShowInactive. The port extraction provides an integration seam, not a Linux activation fix.

- [apps/browser/src/state/windows/windowSync.ts:60](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/windows/windowSync.ts#L60)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1945](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1945)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2016](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2016)

### Implementation slices

1. Honor focus/behind/reuse intent through readiness-aware native activation.
2. Use supported Wayland activation semantics and retain background-instance protection.
3. Coordinate window notification policy with R03.

### Acceptance

- [ ] Ctrl+N and immediate typing target the new window.
- [ ] focus=false and restore-behind never activate unexpectedly.
- [ ] Reuse/utility focus work and background test windows never steal focus.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#46 R03](https://github.com/mckamyk/ardia/issues/46), [#44 R01](https://github.com/mckamyk/ardia/issues/44). These are not additional whole-issue blockers.

Scope: Actual compositor behavior needs physical input; explicit focus after creation can mask the missing option. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## R01 Clicking native split pane B may leave Ardia commands targeting pane A

[#44 R01](https://github.com/mckamyk/ardia/issues/44) · Risk · risk

Reload, close and address commands could act on the previously selected pane after the user clicks inside another native page.

### Current source evidence

Shared onPageFocus now calls activateFocusedPane(tabId); Linux still emits no pageFocus/focus event. Real native WebContents input does not necessarily bubble to outer shell touch handlers. TabPane retains onTouchStart activation, so source inspection alone cannot determine physical split behavior.

- [apps/browser/src/components/window/TabWebView.tsx:221](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/window/TabWebView.tsx#L221)
- [apps/browser/src/components/window/TabPane.tsx:99](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/window/TabPane.tsx#L99)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:464](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L464)
- [apps/linux/src/shims/web/HostWebView.tsx:16](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/web/HostWebView.tsx#L16)

### Implementation slices

1. Reproduce with physical native input before implementing a fix.
2. If confirmed, report native viewport/WebContents focus using stable tab ID and synchronize shared active pane.
3. Coordinate focus/window ownership with F17/R03; do not synthesize focus generations without native handshake support.

### Acceptance

- [ ] Select A, physically click/type B, assert native and shared active target agree.
- [ ] Reload/close/Ctrl+L act on B; test menu/dialog return, keyboard switching, transfers and background windows.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#40 N03](https://github.com/mckamyk/ardia/issues/40), [#27 F17](https://github.com/mckamyk/ardia/issues/27), [#46 R03](https://github.com/mckamyk/ardia/issues/46). These are not additional whole-issue blockers.

Scope: Hypothesis retained, not a confirmed compositor failure. Runtime reproduction of rebased UI requires N01-N04 migration; keyboard pane navigation can still differ from pointer activation.

## R02 Cross-window dragging has coordinate and per-renderer state risks

[#45 R02](https://github.com/mckamyk/ardia/issues/45) · Risk · risk

A tab dragged outside its window could miss its target, create a wrongly positioned window or lose the intended insertion position.

### Current source evidence

Window-drop helper moved to state/drag and still computes bottom-up screen y while Linux returns gfx top-down bounds. Drag state/resolvers remain module-local across separate renderers. Native previews remain unavailable; upstream now calls preview helpers unconditionally.

- [apps/browser/src/state/drag/windowDrop.ts:7](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/drag/windowDrop.ts#L7)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1915](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1915)
- [apps/browser/src/state/drag/tabDrag.ts:142](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/drag/tabDrag.ts#L142)
- [apps/linux/src/hostModules.ts:6](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/hostModules.ts#L6)
- [packages/shell/src/windows.ts:38](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/windows.ts#L38)

### Implementation slices

1. Reproduce on target compositor before claiming exact gesture failure.
2. Define Linux coordinate/target-identity and native DnD session contracts; broker across renderers.
3. Provide optional-helper fallback/capability gating; do not treat y inversion as sufficient on Wayland.

### Acceptance

- [ ] Within-window control and cross-window exact-row insertion.
- [ ] Tear-off, cancellation and mixed-origin/mixed-scale monitors.
- [ ] Native tab identity/playback retained; unsupported global placement handled honestly.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#40 N03](https://github.com/mckamyk/ardia/issues/40), [#30 F20](https://github.com/mckamyk/ardia/issues/30), [#46 R03](https://github.com/mckamyk/ardia/issues/46). These are not additional whole-issue blockers.

Scope: Original physical cross-window failure remains a risk, not a confirmed desktop repro. Explicit native adoption and same-window reorder have paths. New unconditional calls are a separate source-confirmed compatibility hazard. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## R03 Window focus/frame/fullscreen bookkeeping lacks native notifications

[#46 R03](https://github.com/mckamyk/ardia/issues/46) · Risk · risk

The shared focused-window state and saved frame may stay stale after compositor activation, movement/resizing or fullscreen transitions.

### Current source evidence

Native window bookkeeping moved behind WindowPort onEvent; it still needs focus/frame/fullScreen notifications. Linux still emits none and its bounds observer only relayouts the overlay. Restoring old bounds is not evidence of recording user changes.

- [apps/browser/src/state/windows/windowSync.ts:34](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/windows/windowSync.ts#L34)
- [apps/browser/src/state/windows/windowSync.ts:38](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/windows/windowSync.ts#L38)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:642](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L642)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1894](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1894)
- [apps/linux/src/hostSession.ts:87](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/hostSession.ts#L87)

### Implementation slices

1. Observe native widget activation/bounds/fullscreen and deliver owned-window events.
2. Reconcile per-renderer focused-window authority without loops.
3. Distinguish requested/observed geometry and record only meaningful Wayland values.

### Acceptance

- [ ] Physically focus/move/resize/fullscreen two windows and compare store/native/canonical session.
- [ ] Restart reproduces intended sizes/state; document compositor-managed origins.
- [ ] Utility events stay isolated and background test windows remain inactive.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#32 F22](https://github.com/mckamyk/ardia/issues/32), [#31 F21](https://github.com/mckamyk/ardia/issues/31), [#45 R02](https://github.com/mckamyk/ardia/issues/45). These are not additional whole-issue blockers.

Scope: Keep as a desktop verification risk until comparing actual compositor, store and session. Chromium can independently restore native placement. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## Verification for implementation work

Use the current source baseline documented in the index. After N01–N05 restore runnable current-build acceptance, run Linux typecheck/build, focused domain and host fixtures, and actual native feature scenarios. Use physical compositor input for focus/drag/window claims and controlled service or scratch-profile fixtures for network, secrets, import and sync. Preserve native Chromium fallback behavior while testing Ardia’s own user controls.

Do not clean existing Chromium outputs or touch owner profiles. Preserve macOS/demo behavior, current source/API conventions and the no-comments rule for code. This planning document itself has no application tests to pass; a later implementation must record commands, results, current source/binary/UI identities and remaining acceptance requirements in its PR.
