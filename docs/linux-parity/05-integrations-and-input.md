# Linux integrations, input and shared startup services

Draft planning layer 5/6. This workstream describes repairs owned by [#20 F10](https://github.com/mckamyk/ardia/issues/20), [#21 F11](https://github.com/mckamyk/ardia/issues/21), [#22 F12](https://github.com/mckamyk/ardia/issues/22), [#23 F13](https://github.com/mckamyk/ardia/issues/23), [#24 F14](https://github.com/mckamyk/ardia/issues/24), [#28 F18](https://github.com/mckamyk/ardia/issues/28), [#29 F19](https://github.com/mckamyk/ardia/issues/29), [#30 F20](https://github.com/mckamyk/ardia/issues/30), [#37 F27](https://github.com/mckamyk/ardia/issues/37). It adds no application behavior and closes no repair issue. The Git base chain is document order; use the [coordination tracker](https://github.com/mckamyk/ardia/issues/10) and [index](README.md) for actual blockers and parallel lanes.

## Integration contract

Suggested role: **Linux integration and input owners**. File boundary: Linux-only network/import/sync/calendar/startup/input helpers; coordinate shims/shellBase.tsx and main.tsx with their single integrator. Independent parsers and fixtures need no shared-file ownership.

- Narrow network and protected credential operations.
- App-wide versus renderer-local service ownership.
- Portable import/sync/calendar provider APIs.
- Pointer modifiers and native drag sessions.
- Safe lifecycle policy prerequisites.

Split this workstream into network/credential, portable import, sync, calendar, input and startup lanes. Do not start one app-wide sync/policy writer per renderer. Provider interfaces and fixtures can start before authenticated service/compositor environments are available. F18 must decline automatic discard when unsaved work or protection state is unknown. F20 inbound drop is independent of Downloads; its outbound downloaded-file acceptance waits for F08.

## F10 Online search suggestions are blocked by the production shell CSP

[#20 F10](https://github.com/mckamyk/ardia/issues/20) · P2 · persists

Turning on search suggestions does not enable remote suggestions in the Linux command/address bar. Local tabs, history and bookmark suggestions remain distinct sources.

### Current source evidence

Omnibox still gives renderer fetch to createSuggestFetcher; production chrome://ardia retains connect-src none. No narrow Linux suggestion request service replaces that path. Domain refactoring does not change the CSP constraint.

- [apps/browser/src/components/omnibox/useSuggestions.ts:98](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/omnibox/useSuggestions.ts#L98)
- [packages/core/src/suggestFetcher.ts:55](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/core/src/suggestFetcher.ts#L55)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:788](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L788)

### Implementation slices

1. Provide a narrow policy-aware native suggestion request path with bounded replies and provider validation.
2. Preserve cancellation/privacy/disabled state; keep privileged CSP narrow.

### Acceptance

- [ ] Packaged UI receives controlled online suggestions only when enabled.
- [ ] Disabled sends zero requests; stale/cancelled/malformed/oversized/offline replies cannot replace current suggestions.
- [ ] Do not introduce unrestricted privileged fetch or inadvertently forward cookies/secrets.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#21 F11](https://github.com/mckamyk/ardia/issues/21). These are not additional whole-issue blockers.

Scope: Local tabs/history/bookmarks are separate sources; their new transport additionally depends on N02. No runtime claim for the presently unbuildable UI.

## F11 Live folders render, but service connection and refresh are unavailable

[#21 F11](https://github.com/mckamyk/ardia/issues/21) · P2 · persists

GitHub/Bitbucket pull requests and Notion/Confluence/Drive documents cannot connect or refresh real service data despite their Settings controls.

### Current source evidence

Live startup remains present through sidebar effects. Service HTTP calls still use renderer fetch under connect-src none; ArdiaKeychain credentials still have no Linux handler. Moving networking/state behind platform reexports did not supply Linux implementations.

- [apps/browser/src/state/sidebar/effects.ts:18](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/sidebar/effects.ts#L18)
- [apps/browser/src/state/live/net.ts:26](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/live/net.ts#L26)
- [apps/browser/src/state/live/net.ts:64](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/live/net.ts#L64)
- [packages/shell/src/live.ts:58](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/live.ts#L58)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:788](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L788)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2074](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2074)

### Implementation slices

1. Add narrow bounded/cancellable Linux service networking; keep privileged shell CSP restrictive.
2. Implement protected secret storage and supported sign-in/refresh flows.
3. Coordinate network API with F10, secret storage with F07/F13 and document integration with F04.

### Acceptance

- [ ] Connect, refresh, restart and disconnect each advertised provider using fixtures.
- [ ] Test revoked tokens, renewal, offline/retry and cancellation.
- [ ] Ordinary pages cannot invoke privileged network/secret operations.

### Dependencies and parallel handoff

Blocked for full acceptance by: [#14 F04](https://github.com/mckamyk/ardia/issues/14).

No issue-wide downstream block is declared.

- Real service data must reconcile across Settings/browser renderers and survive close/restart without stale overwrites.

Coordinate contracts/shared files with [#14 F04](https://github.com/mckamyk/ardia/issues/14), [#17 F07](https://github.com/mckamyk/ardia/issues/17), [#20 F10](https://github.com/mckamyk/ardia/issues/20), [#24 F14](https://github.com/mckamyk/ardia/issues/24). These are not additional whole-issue blockers.

Scope: Persisted cached rows can render. This is not a missing startLive call. F04 remains a separate persistence defect. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## F12 Import has a real utility window but no data-import backend

[#22 F12](https://github.com/mckamyk/ardia/issues/22) · P2 · persists

Browser discovery yields an empty/error chooser, and Linux-relevant bookmark HTML and password CSV imports cannot run.

### Current source evidence

Import flow was split into a hook/actions and importModule moved to platform. The Linux shim still merely reexports ArdiaImport APIs; discovery, portable-file selection/parsing, import jobs and cancellation have no native handlers.

- [apps/linux/src/shims/import.tsx:1](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/import.tsx#L1)
- [packages/import/src/index.ts:212](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/import/src/index.ts#L212)
- [packages/import/src/index.ts:227](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/import/src/index.ts#L227)
- [apps/browser/src/platform/importModule.ts:23](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/platform/importModule.ts#L23)
- [apps/browser/src/components/import/useImportFlow.ts:42](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/import/useImportFlow.ts#L42)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2074](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2074)

### Implementation slices

1. Implement portable HTML/CSV import and Linux browser discovery/readers against scratch fixtures.
2. Define native chooser, progress/cancellation and short-lived decrypted import payloads.
3. Separate portable parsing from protected credential/decryption integration.

### Acceptance

- [ ] Import nested bookmarks and CSV credentials with accurate counts/deduplication.
- [ ] Import scratch Chromium/Firefox profiles with partial errors and cancellation.
- [ ] Source files are unchanged; temporary secrets and cancelled jobs are cleaned up.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#17 F07](https://github.com/mckamyk/ardia/issues/17), [#18 F08](https://github.com/mckamyk/ardia/issues/18), [#23 F13](https://github.com/mckamyk/ardia/issues/23), [#36 F26](https://github.com/mckamyk/ardia/issues/36). These are not additional whole-issue blockers.

Scope: Do not require Safari/Apple automation on Linux. Generic HTML/CSV and supported Linux browsers remain required. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## F13 Encrypted folder sync is unavailable

[#23 F13](https://github.com/mckamyk/ardia/issues/23) · P2 · persists

Linux cannot create/join an encrypted sync folder or synchronize browser data with another device.

### Current source evidence

Sync state was split into several modules but still relies on optional ArdiaSync. Linux advertises no module/capabilities and starts no sync service, so the shared status remains unavailable.

- [packages/sync/src/native.ts:53](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/sync/src/native.ts#L53)
- [apps/linux/src/shims/expo-modules-core.ts:86](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/expo-modules-core.ts#L86)
- [apps/browser/src/state/sync/engine.ts:41](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/sync/engine.ts#L41)
- [apps/browser/src/components/settings/panes/Sync.tsx:38](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/settings/panes/Sync.tsx#L38)
- [apps/linux/src/main.tsx:40](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/main.tsx#L40)

### Implementation slices

1. Implement Linux secure storage, compatible cryptography/recovery and atomic folder transport.
2. Choose one application/profile authority for sync rather than racing each renderer.
3. Integrate profile resolution and canonical document/state handling.

### Acceptance

- [ ] Linux-Linux and Linux-macOS encrypted fixture exchange and concurrent/offline merge.
- [ ] Wrong phrase/tampering/locked storage fail safely; recovery and interruption/restart work.
- [ ] No cleartext credential material appears in sync artifacts.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#12 F02](https://github.com/mckamyk/ardia/issues/12), [#14 F04](https://github.com/mckamyk/ardia/issues/14), [#17 F07](https://github.com/mckamyk/ardia/issues/17), [#22 F12](https://github.com/mckamyk/ardia/issues/22). These are not additional whole-issue blockers.

Scope: The unavailable UI is honest. Preserve existing encrypted formats and new sync document version/migration rules. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## F14 Calendar previews, countdowns and meeting alerts lack a Linux calendar backend

[#24 F14](https://github.com/mckamyk/ardia/issues/24) · P2 · persists

Pinned calendar sites cannot supply the promised native previews or upcoming-meeting alerts. The denied-state button sends users to a macOS-only Settings URL.

### Current source evidence

Calendar authorization remains hardcoded denied; no Linux access/calendar/event provider exists. The shared denied-state UI still links macOS System Settings. Shared Calendar state moved without adding a Linux backend.

- [apps/linux/src/hostModules.ts:17](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/hostModules.ts#L17)
- [packages/shell/src/live.ts:37](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/live.ts#L37)
- [apps/browser/src/state/live/calendar.ts:101](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/live/calendar.ts#L101)
- [apps/browser/src/components/settings/panes/Calendar.tsx:36](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/settings/panes/Calendar.tsx#L36)
- [apps/browser/src/components/settings/panes/Calendar.tsx:41](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/settings/panes/Calendar.tsx#L41)

### Implementation slices

1. Define Linux calendar provider/status capabilities and select EDS/CalDAV or another supported backend.
2. Replace macOS account/permission instructions and URLs.
3. Map calendar events into shared state and include its settings in F04 synchronization.

### Acceptance

- [ ] Recurring/time-zone/all-day/declined/cancelled fixtures render correctly.
- [ ] Filtering, countdowns, Join alerts, disconnect and permission errors work.
- [ ] Settings changes reach browser windows and persist immediately on close.

### Dependencies and parallel handoff

Blocked for full acceptance by: [#14 F04](https://github.com/mckamyk/ardia/issues/14).

No issue-wide downstream block is declared.

- Calendar preference acceptance includes synchronized and durably flushed multiwindow settings.

Coordinate contracts/shared files with [#14 F04](https://github.com/mckamyk/ardia/issues/14), [#21 F11](https://github.com/mckamyk/ardia/issues/21), [#36 F26](https://github.com/mckamyk/ardia/issues/36). These are not additional whole-issue blockers.

Scope: Calendar websites still browse normally. Backend choice is a product/platform decision; gating unavailable controls can proceed first. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## F18 Memory saver, freezing and battery-aware tab policies are not started

[#28 F18](https://github.com/mckamyk/ardia/issues/28) · P2 · persists

Linux does not run the shared sleep/freeze/battery-saving policies represented by the app’s settings. Its similarly named host tab-lifecycle service handles explicit close/unload ownership instead.

### Current source evidence

Current browser main still starts resource lifecycle and memory trim; Linux starts only explicit tab close/unload ownership. Shared policy needs unsupported evaluate/discard/setFrozen and engine.tasks/systemState/batterySaver plus synchronous input-idle facilities, now using domain architecture.

- [apps/browser/src/main.tsx:35](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/main.tsx#L35)
- [apps/browser/src/main.tsx:42](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/main.tsx#L42)
- [apps/linux/src/main.tsx:43](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/main.tsx#L43)
- [apps/browser/src/state/tabs/tabLifecycle.ts:66](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/tabs/tabLifecycle.ts#L66)
- [apps/browser/src/state/tabs/tabLifecycle.ts:265](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/tabs/tabLifecycle.ts#L265)

### Implementation slices

1. Inventory per-process/per-renderer startup and register supported shared policies once.
2. Implement safe unsaved-work query, discard/wake/freeze and power/memory/task/input-idle APIs.
3. Hard-gate automatic discard on reliable dirty-form, prompt, media/capture/PiP and visibility protections; coordinate F15/F17.

### Acceptance

- [ ] Safe idle tabs sleep/freeze/wake with navigation intact.
- [ ] Dirty forms, visible splits, active media/capture/PiP and pending prompts remain protected.
- [ ] Unknown state fails safely; mocked pressure/power transitions and restart do not double-register policies.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#25 F15](https://github.com/mckamyk/ardia/issues/25), [#26 F16](https://github.com/mckamyk/ardia/issues/26), [#27 F17](https://github.com/mckamyk/ardia/issues/27), [#37 F27](https://github.com/mckamyk/ardia/issues/37). These are not additional whole-issue blockers.

Scope: Chromium background throttling is separate. Dirty-input uncertainty still declines destructive sleep. Do not enable policies until protections are reliable.

## F19 Ctrl-click is not translated for shared tab and bookmark actions

[#29 F19](https://github.com/mckamyk/ardia/issues/29) · P2 · persists

Ctrl-clicking a bookmark can replace the current page instead of opening a background tab. Ctrl-click multiselection in sidebar/top tabs fails; Ctrl+Shift-click can take the Shift-only new-window route.

### Current source evidence

Upstream centralized click modifiers in lib/keys and selection logic in state/drag; these types/helpers still recognize metaKey/shiftKey/altKey and omit ctrlKey. Linux Pressable still forwards physical DOM modifiers without primary-button normalization.

- [apps/linux/src/shims/web/motion/macView.tsx:202](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/web/motion/macView.tsx#L202)
- [apps/browser/src/lib/keys.ts:3](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/lib/keys.ts#L3)
- [apps/browser/src/state/bookmarks/actions.ts:11](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/bookmarks/actions.ts#L11)
- [apps/browser/src/state/drag/drops.ts:34](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/drag/drops.ts#L34)
- [apps/browser/src/state/drag/drops.ts:160](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/drag/drops.ts#L160)

### Implementation slices

1. Normalize Linux primary pointer modifier at an explicit Linux boundary or injected helper.
2. Preserve physical modifiers for recording/native page input and avoid treating Super as Ctrl.
3. Use the same normalized behavior for sidebar, top tabs and bookmark clicks.

### Acceptance

- [ ] Ctrl-click opens bookmarks in background; Ctrl+Shift opens foreground.
- [ ] Ctrl selection toggle and Shift ranges work in sidebar/top tabs.
- [ ] Middle-click/text editing/native webpage modifiers remain unchanged.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#43 N06](https://github.com/mckamyk/ardia/issues/43), [#30 F20](https://github.com/mckamyk/ardia/issues/30). These are not additional whole-issue blockers.

Scope: Middle-click, Shift range and ordinary web-page input are separate. This persists despite the useful upstream helper consolidation. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## F20 External drop into browser chrome and file drag out are absent

[#30 F20](https://github.com/mckamyk/ardia/issues/30) · P2 · persists

Dropping URLs/files/text onto the sidebar or top strip never reaches the shared tab-drop parser. Dragging a downloaded file out of Ardia has no exported file path.

### Current source evidence

UPDATED MECHANISM: upstream removed the metadata fallback and now always requests ArdiaFileDrop. Linux requireNativeViewManager returns a placeholder View with no drag/drop producer, so shared onDropUpdate still never runs. Outbound MouseArea still discards path.

- [packages/shell/src/system.tsx:87](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/system.tsx#L87)
- [packages/shell/src/system.tsx:117](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/system.tsx#L117)
- [apps/linux/src/shims/expo-modules-core.ts:93](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/expo-modules-core.ts#L93)
- [apps/browser/src/components/window/TabDropArea.tsx:105](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/window/TabDropArea.tsx#L105)
- [apps/linux/src/shims/web/MouseArea.tsx:12](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/web/MouseArea.tsx#L12)
- [apps/linux/src/shims/web/motion/macView.tsx:158](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/web/motion/macView.tsx#L158)

### Implementation slices

1. Provide Linux FileDropArea with DOM URL/text handling and native brokered file payloads.
2. Implement outbound native downloaded-file drag with validated path/token ownership.
3. Honor ordered enter/move/exit/drop/cancel coordinates and payload bounds.

### Acceptance

- [ ] Drop URL, text, files and multiple items onto supported sidebar/top-strip positions.
- [ ] Drag a downloaded scratch file into a scratch target application.
- [ ] Malformed payload/cancel works; internal reorder/page drop remain valid.

### Dependencies and parallel handoff

Blocked for full acceptance by: [#18 F08](https://github.com/mckamyk/ardia/issues/18).

No issue-wide downstream block is declared.

- The outbound downloaded-file UI cannot be accepted end to end while Downloads has no items; inbound drop/backend work can start immediately.

Coordinate contracts/shared files with [#18 F08](https://github.com/mckamyk/ardia/issues/18), [#45 R02](https://github.com/mckamyk/ardia/issues/45), [#29 F19](https://github.com/mckamyk/ardia/issues/29). These are not additional whole-issue blockers.

Scope: Do not repeat the old metadata-check evidence. Internal PanResponder reorder and webpage upload-drop are distinct. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## F27 App-level help/onboarding and developer services are not started

[#37 F27](https://github.com/mckamyk/ardia/issues/37) · P2 · persists

Automatic onboarding/version tracking and handlers for app-level commands such as feedback, diagnostics, performance recording, release notes and task manager are absent from Linux startup. CLI control and watched user scripts have no backend.

### Current source evidence

Linux still starts only its historical subset and imports WindowRoot rather than browser main. App-level command/onboarding/first-window service registration remains absent. Incoming setup-code UI is bundled but does not start onboarding; incoming synchronous LaunchConfig also needs a Linux provider.

- [apps/browser/src/main.tsx:34](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/main.tsx#L34)
- [apps/browser/src/main.tsx:40](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/main.tsx#L40)
- [apps/browser/src/main.tsx:59](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/main.tsx#L59)
- [apps/linux/src/main.tsx:40](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/main.tsx#L40)
- [apps/browser/src/state/app/appIntegration.ts:39](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/app/appIntegration.ts#L39)
- [apps/browser/src/state/onboarding/onboarding.ts:378](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/onboarding/onboarding.ts#L378)
- [apps/browser/src/state/presets/actions.ts:50](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/presets/actions.ts#L50)
- [apps/browser/src/platform/launchConfig.ts:5](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/platform/launchConfig.ts#L5)

### Implementation slices

1. Inventory startup service ownership and add a Linux startup registry/command adapter.
2. Provide a synchronous validated LaunchConfig; signal firstWindowCommitted exactly once per intended owner.
3. Register supported help/onboarding/version/task tools; gate unavailable CLI/user-script/music integrations deliberately.

### Acceptance

- [ ] Fresh profile onboarding starts once; completed/resumed cases behave correctly.
- [ ] Visible app commands work from correct browser/utility ownership.
- [ ] Setup code preserves search/privacy choices and applies layouts/pins across windows without duplicate services.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#41 N04](https://github.com/mckamyk/ardia/issues/41), [#15 F05](https://github.com/mckamyk/ardia/issues/15), [#19 F09](https://github.com/mckamyk/ardia/issues/19), [#28 F18](https://github.com/mckamyk/ardia/issues/28), [#43 N06](https://github.com/mckamyk/ardia/issues/43). These are not additional whole-issue blockers.

Scope: Do not blindly start Apple scripting/telemetry. Setup code application itself is shared pure state and is not established broken merely because onboarding startup is missing. N01 and the new LaunchConfig/domain prerequisites must land before runtime acceptance. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## Verification for implementation work

Use the current source baseline documented in the index. After N01–N05 restore runnable current-build acceptance, run Linux typecheck/build, focused domain and host fixtures, and actual native feature scenarios. Use physical compositor input for focus/drag/window claims and controlled service or scratch-profile fixtures for network, secrets, import and sync. Preserve native Chromium fallback behavior while testing Ardia’s own user controls.

Do not clean existing Chromium outputs or touch owner profiles. Preserve macOS/demo behavior, current source/API conventions and the no-comments rule for code. This planning document itself has no application tests to pass; a later implementation must record commands, results, current source/binary/UI identities and remaining acceptance requirements in its PR.
