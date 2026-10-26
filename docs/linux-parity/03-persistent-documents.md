# Linux canonical document state and save-before-close

Draft planning layer 3/6. This workstream describes repairs owned by [#14 F04](https://github.com/mckamyk/ardia/issues/14). It adds no application behavior and closes no repair issue. The Git base chain is document order; use the [coordination tracker](https://github.com/mckamyk/ardia/issues/10) and [index](README.md) for actual blockers and parallel lanes.

## Integration contract

Suggested role: **Canonical document and persistence owner**. File boundary: apps/linux/src/hostDocuments.ts; hostSession.ts; utilityClose.ts; Linux native document worker. Coordinate a small state/live persistence adapter hook; never race separate owners over the whole document.

- Profile/document revisions and identity-aware merge.
- Canonical notifications without write echoes.
- Pending debounce and native write close barrier.

Implement the document protocol independently of Live networking and Calendar provider work. Versioned JSON serialization alone does not solve per-renderer stale copies or whole-document writes. Include tombstones/identity merge and drain pending store timers before native close acknowledgment. Consumers may develop against this protocol while F04 is in progress.

## F04 Live-folder and Calendar edits can stay stale, disappear or overwrite another window

[#14 F04](https://github.com/mckamyk/ardia/issues/14) · P1 · persists

A setting changed in the utility window does not reach an already open browser. Later whole-document saves can restore deleted folders or overwrite preferences. Closing Settings before its debounce expires can lose the edit.

### Current source evidence

live.json moved into state/live and gained versioned JSON serialization; it still loads one module-local store and writes whole snapshots after 800ms. Linux caches remain per-renderer, document writes have no broadcast/merge, and utility close flushes only session persistence.

- [apps/browser/src/state/live/store.ts:63](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/live/store.ts#L63)
- [apps/browser/src/state/live/store.ts:105](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/live/store.ts#L105)
- [apps/linux/src/hostDocuments.ts:4](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/hostDocuments.ts#L4)
- [apps/linux/src/hostSession.ts:67](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/hostSession.ts#L67)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1813](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1813)
- [apps/linux/src/utilityClose.ts:9](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/utilityClose.ts#L9)

### Implementation slices

1. Define revisioned document snapshots, canonical updates and identity-aware live-folder merge/tombstones.
2. Update host caches and live stores across renderers without echo writes.
3. Register pending document timers/writes with save-before-close; retain live.json version and migration metadata.

### Acceptance

- [ ] Two browser windows and Settings converge after preference edits and folder creation/deletion.
- [ ] Close immediately and after 800ms; restart preserves the same final state.
- [ ] Concurrent edits survive; save failures retain the utility for retry.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

Blocks full acceptance of: [#21 F11](https://github.com/mckamyk/ardia/issues/21), [#24 F14](https://github.com/mckamyk/ardia/issues/24).

Coordinate contracts/shared files with [#21 F11](https://github.com/mckamyk/ardia/issues/21), [#24 F14](https://github.com/mckamyk/ardia/issues/24), [#23 F13](https://github.com/mckamyk/ardia/issues/23), [#31 F21](https://github.com/mckamyk/ardia/issues/31), [#37 F27](https://github.com/mckamyk/ardia/issues/37). These are not additional whole-issue blockers.

Scope: Schema versioning does not fix stale caches or concurrent whole-document overwrites. Pre-rebase adapter reproduction showed disk/Settings false and browser cache true. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## Verification for implementation work

Use the current source baseline documented in the index. After N01–N05 restore runnable current-build acceptance, run Linux typecheck/build, focused domain and host fixtures, and actual native feature scenarios. Use physical compositor input for focus/drag/window claims and controlled service or scratch-profile fixtures for network, secrets, import and sync. Preserve native Chromium fallback behavior while testing Ardia’s own user controls.

Do not clean existing Chromium outputs or touch owner profiles. Preserve macOS/demo behavior, current source/API conventions and the no-comments rule for code. This planning document itself has no application tests to pass; a later implementation must record commands, results, current source/binary/UI identities and remaining acceptance requirements in its PR.
