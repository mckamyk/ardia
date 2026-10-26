# Linux upstream baseline, capabilities and profile boundaries

Draft planning layer1/6. This workstream describes repairs owned by [#38 N01](https://github.com/mckamyk/ardia/issues/38), [#39 N02](https://github.com/mckamyk/ardia/issues/39), [#40 N03](https://github.com/mckamyk/ardia/issues/40), [#41 N04](https://github.com/mckamyk/ardia/issues/41), [#42 N05](https://github.com/mckamyk/ardia/issues/42), [#43 N06](https://github.com/mckamyk/ardia/issues/43), [#12 F02](https://github.com/mckamyk/ardia/issues/12), [#36 F26](https://github.com/mckamyk/ardia/issues/36). It adds no application behavior and closes no repair issue. The Git base chain is document order; use the [coordination tracker](https://github.com/mckamyk/ardia/issues/10) and [index](README.md) for actual blockers and parallel lanes.

## Integration contract

Suggested role: **Linux baseline and capability integrator**. File boundary: apps/linux/vite.config.ts; vite.shared.ts; hostModules.ts; shims/expo-modules-core.ts; shims/ardiacore.tsx; HostWebView.tsx; engine/ardiacore-linux helper modules. One designated owner integrates changes to ac_linux_host.cc.

- Stable domain/verb JSON and error/event envelope.
- Synchronous launch configuration and real host capability contract.
- Application tab ID versus native transport ID.
- Profile resolver and isolation authority.
- One effective Linux shortcut catalog.

N01–N05 are new runnable-baseline prerequisites after main’s311 incoming commits. Agree their interfaces first, then implement separate Vite/import, domain-routing, identity, launch-config and native-build slices concurrently. N06 corrects shortcut defaults/display independently. F02 first gates unsupported identities safely, then supplies real profile/private ownership; F26 treats absent integrations honestly. Do not claim a compile-only fix completes this baseline.

The native build requires Chromium156 and aligned Helium pins. Existing155 binaries are historical artifacts. Keep app/engine contracts current together rather than adding shared aliases for removed ArdiaCEF methods.

## N01 Migrate Linux imports, Vite adapters and type contracts after the upstream layer move

[#38 N01](https://github.com/mckamyk/ardia/issues/38) · P1 · new

The current Linux UI cannot build or typecheck, and three host-test files fail to load. A new Linux UI cannot be packaged until these entry points and tests target the current browser graph.

### Current source evidence

The rebase imports 311 upstream commits that move browser/src/store, lib and component state into state/ and platform/, split feature components, and tighten native types. Linux still imports removed paths and Vite replacement/lazy maps name removed files. The current production build fails resolving browser/src/components/settings/windows from src/settingsWindows.ts. Typecheck reports 105 errors, including obsolete imports, removed WebView props, shortcut modifier typing and a nullable fetchBangList reply.

- [apps/linux/src/main.tsx:7](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/main.tsx#L7)
- [apps/linux/src/settingsWindows.ts:2](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/settingsWindows.ts#L2)
- [apps/linux/vite.config.ts:6](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/vite.config.ts#L6)
- [apps/linux/vite.shared.ts:62](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/vite.shared.ts#L62)
- [apps/linux/scripts/host-test-loader.mjs:1](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/scripts/host-test-loader.mjs#L1)
- [apps/browser/src/platform/launchConfig.ts:1](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/platform/launchConfig.ts#L1)
- [apps/browser/src/state/browser/store.ts:1](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/browser/store.ts#L1)

### Implementation slices

1. Map every Linux import to the current state/platform/feature owner; update test loaders, Vite component replacements and lazy chunk paths with the same mapping.
2. Adapt Linux component wrappers to current shared exports and exact synchronous/Promise return types; preserve disabled Linux New Tab prewarming through the current switch/config boundary.
3. Keep one canonical browser store and native-module/menu instance in the bundle; use Linux-owned adapters instead of changing the demo or adding compatibility aliases to shared browser files.
4. Fix nullability and ShortcutModifier return contracts, then run the real production module graph and host tests. Coordinate HostWebView prop changes with N03.

### Acceptance

- [ ] pnpm --filter @ardia/linux build and typecheck pass on the rebased source without unresolved old paths or implicit-any fallout.
- [ ] All Linux host test files load and run; production bundle contains no apps/demo modules or duplicate browser stores.
- [ ] Linux replacement components, native WebView, utility navigation, menu and symbol modules resolve exactly once.
- [ ] Current-source native runtime bootstrap is subsequently verified with N02–N05; a build-only pass must not be called feature parity.

### Dependencies and parallel handoff

Blocked for full acceptance by: [#40 N03](https://github.com/mckamyk/ardia/issues/40).

No issue-wide downstream block is declared.

- The whole Linux typecheck/build acceptance includes the removed transferKey/ready prop contract; path migration can start before stable-ID integration is complete.

Coordinate contracts/shared files with [#39 N02](https://github.com/mckamyk/ardia/issues/39), [#40 N03](https://github.com/mckamyk/ardia/issues/40), [#41 N04](https://github.com/mckamyk/ardia/issues/41), [#42 N05](https://github.com/mckamyk/ardia/issues/42), [#43 N06](https://github.com/mckamyk/ardia/issues/43). These are not additional whole-issue blockers.

Scope: These are rebase integration failures, not evidence that upstream macOS is broken. Do not restore obsolete shared files or point Linux back to an older/duplicate store to make imports resolve. The native ABI, tab identity and launch configuration have separate migration issues.

## N02 Rebased shared UI cannot use the Linux legacy native call and event ABI

[#39 N02](https://github.com/mckamyk/ardia/issues/39) · P1 · persists

Rebased shared UI cannot use the Linux legacy native call and event ABI

### Current source evidence

Upstream now requires per-domain Ardia*.call(verb,profile,JSONargs) returning a JSON string and rejecting errors. Events subscribe to domain channels with {name,data}. Linux intercepts only obsolete ArdiaCEF.engineCall and emits legacy ArdiaCEF.onEngineEvent/onTabStrip. Current history/bookmarks/strip/engine/extension/download-setting and auto-PiP calls cannot use existing backends. Raw C {error} replies also must become rejections rather than apparent success.

- [packages/ardiacore/src/engine/call.ts:24](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/engine/call.ts#L24)
- [packages/ardiacore/src/engine/call.ts:47](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/engine/call.ts#L47)
- [packages/ardiacore/src/engine/call.ts:68](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/engine/call.ts#L68)
- [apps/linux/src/shims/ardiacore.tsx:8](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/ardiacore.tsx#L8)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1181](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1181)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1835](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1835)

### Implementation slices

1. Implement allowlisted current domain/verb mapping to existing engine/native functions; validate args and preserve separate profile.
2. Serialize scalar/null/object replies once and reject engine errors with stable codes.
3. Translate current domain event envelopes with profile/tab ownership filtering; preserve dedicated secret native methods.

### Acceptance

- [ ] Contract tests cover JSON string/scalar/null/error handling and domain event subscriptions/removal.
- [ ] Current shared history/bookmarks/strips/extension list/download settings/auto-PiP paths reach real supported native services.
- [ ] Unknown/private profile and ordinary-page privileged access remain rejected.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#40 N03](https://github.com/mckamyk/ardia/issues/40), [#41 N04](https://github.com/mckamyk/ardia/issues/41), [#13 F03](https://github.com/mckamyk/ardia/issues/13), [#16 F06](https://github.com/mckamyk/ardia/issues/16), [#17 F07](https://github.com/mckamyk/ardia/issues/17), [#18 F08](https://github.com/mckamyk/ardia/issues/18), [#19 F09](https://github.com/mckamyk/ardia/issues/19), [#25 F15](https://github.com/mckamyk/ardia/issues/25), [#27 F17](https://github.com/mckamyk/ardia/issues/27). These are not additional whole-issue blockers.

Scope: Source-confirmed compatibility regression; N01 currently prevents runtime boot, so this is not a fresh packaged runtime result. Preserve default-profile fail-closed policy, typed secrets and explicit unsupported errors.

## N03 Rebased tabId contract diverges from Linux random native binding keys

[#40 N03](https://github.com/mckamyk/ardia/issues/40) · P1 · persists

Rebased tabId contract diverges from Linux random native binding keys

### Current source evidence

Current shared WebView supplies tabId and zero-argument onReady; Linux reads removed transferKey, falls back to random host_UUID and uses missing transferKey for overlay checks. Native binding keys therefore diverge from store tab IDs. Current permissions/media/extension/task events also use stable string tab IDs, while Linux transport still works with numeric IDs internally.

- [packages/ardiacore/src/pageTypes.ts:147](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/pageTypes.ts#L147)
- [apps/browser/src/components/window/TabWebView.tsx:121](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/window/TabWebView.tsx#L121)
- [apps/linux/src/shims/web/HostWebView.tsx:26](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/web/HostWebView.tsx#L26)
- [apps/linux/src/shims/web/HostWebView.tsx:112](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/web/HostWebView.tsx#L112)
- [packages/ardiacore/src/module.ts:33](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/module.ts#L33)

### Implementation slices

1. Use props.tabId for stable native binding/claim/overlay identity and zero-argument readiness.
2. Maintain explicit native-ID-to-tab-key lookup for current service calls/events and ownership.
3. Adapt fixtures to assert native binding key equals store tab ID across remount/adopt/transfer.

### Acceptance

- [ ] Create/close/remount/adopt/transfer retains exactly one correct native tab and event owner.
- [ ] Shared strip selection and close-by-key operate on the requested tab.
- [ ] Overlay checks use correct owner; service events target stable tab IDs without leaking across windows/profiles.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

Blocks full acceptance of: [#38 N01](https://github.com/mckamyk/ardia/issues/38).

Coordinate contracts/shared files with [#44 R01](https://github.com/mckamyk/ardia/issues/44), [#45 R02](https://github.com/mckamyk/ardia/issues/45), [#12 F02](https://github.com/mckamyk/ardia/issues/12). These are not additional whole-issue blockers.

Scope: Do not globally remove numeric IDs: strip/window/native transport identifiers remain numeric. Identity mismatch is source-established, but physical effects need runtime after bootstrap repairs.

## N04 Synchronous launchConfig contract has no Linux provider and can break startup

[#41 N04](https://github.com/mckamyk/ardia/issues/41) · P1 · persists

Synchronous launchConfig contract has no Linux provider and can break startup

### Current source evidence

New synchronous ArdiaApp.launchConfig() returns LaunchConfig; shared switchOverride immediately indexes result.switches. Linux has no launchConfig local provider or synchronous host mapping and returns an async hostModuleMethod for unknown methods, so code indexes a Promise and native invocation later rejects. Existing systemInfo has a Linux local adapter and should not be falsely counted as equally missing.

- [packages/shell/src/app.ts:49](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/app.ts#L49)
- [packages/shell/src/app.ts:145](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/app.ts#L145)
- [apps/browser/src/platform/launchConfig.ts:5](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/platform/launchConfig.ts#L5)
- [apps/browser/src/platform/killSwitches.ts:127](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/platform/killSwitches.ts#L127)
- [apps/linux/src/hostModules.ts:16](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/hostModules.ts#L16)
- [apps/linux/src/shims/shellBase.tsx:85](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/shellBase.tsx#L85)

### Implementation slices

1. Provide bootstrap-backed synchronous LaunchConfig with complete dataDirectory/background/isolated/testInstance/instance/switches/trace/test fields.
2. Use intentional Linux defaults, retain disabled new-tab prewarm, and audit other synchronous methods.
3. Test before module import/render, rather than awaiting a native Promise.

### Acceptance

- [ ] launchConfig returns plain complete object synchronously; switchOverride and test access do not throw.
- [ ] Fresh and restored production UI starts without unsupported launchConfig rejection.
- [ ] Linux prewarm/focus safeguards and experiment defaults remain deliberate; no environment secrets exposed.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#37 F27](https://github.com/mckamyk/ardia/issues/37), [#42 N05](https://github.com/mckamyk/ardia/issues/42). These are not additional whole-issue blockers.

Scope: Source-confirmed boot hazard once moved imports resolve. Do not make synchronous API shape correct by silently enabling unsupported experiments or exposing process secrets.

## N05 Provide Linux launch switches and qualify the Chromium 156 native build and package

[#42 N05](https://github.com/mckamyk/ardia/issues/42) · P1 · new

A matching Linux native engine is not established for the rebased source. The shared engine now depends on a Mac-only GN target/source, so the existing Chromium 155 package cannot stand in for a current native build.

### Current source evidence

Main advances Chromium to156.0.8078.47 and shared Helium0.20.1. The rebased shared Ardia BUILD depends on //ardia/core:launch_switches, whose BUILD asserts is_mac and whose ac_launch_switches.cc includes Apple Security frameworks. Linux apply copies only the hook headers, not the new launch-switch contract. ac_downloads.cc now includes that header. Linux pins have been aligned to HeliumLinux0.20.1.1 (473c0f72…), whose shared submodule matches f29a2596…; no Linux156 build/strict series/package qualification was run. One tooling test still hardcodes155 and fails after the corrected pin.

- [engine/patches/build/helium-pins.ini:1](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/patches/build/helium-pins.ini#L1)
- [engine/chromium/src/chrome/browser/ardia/BUILD.gn:110](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/chromium/src/chrome/browser/ardia/BUILD.gn#L110)
- [engine/ardiacore/src/ardia/core/BUILD.gn:1](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore/src/ardia/core/BUILD.gn#L1)
- [engine/ardiacore/src/ardia/core/ac_launch_switches.cc:3](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore/src/ardia/core/ac_launch_switches.cc#L3)
- [engine/chromium/src/chrome/browser/ardia/ac_downloads.cc:13](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/chromium/src/chrome/browser/ardia/ac_downloads.cc#L13)
- [engine/ardiacore-linux/apply.sh:27](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/apply.sh#L27)
- [engine/patches/build/tests/test_platform_tooling.py:197](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/patches/build/tests/test_platform_tooling.py#L197)

### Implementation slices

1. Supply a Linux implementation of the shared launch-switch contract and select its GN target under is_linux; retain the existing macOS implementation and behavior.
2. Extend Linux layer materialization/drift manifests to include the required shared declarations and Linux implementation. Reconcile all156 hook signatures, selected patch contexts and GN dependencies on a separate/version-correct source tree.
3. Update version-sensitive fixture expectations meaningfully, retaining the Linux/shared Helium compatibility check and failing closed for unavailable OS authentication.
4. Prepare a matching156 tree without cleaning or synchronizing an existing build cache, compile incrementally under the Chromium lock, package the current Linux UI, and retain engine-tree/build provenance.

### Acceptance

- [ ] GN generation and header checks resolve Linux launch-switch dependencies without evaluating the is_mac-only target.
- [ ] Strict Linux patch/materialization checks and platform tooling pass for the selected156 pins; no Mac-only changes leak into Linux or vice versa.
- [ ] A real matching Linux156 engine/package boots with scratch profiles, Chromium sandbox, ordinary-page bridge isolation and correct native service ABI.
- [ ] Re-run native keyboard/restored-session/auto-PiP checks with the migrated UI and record binary/UI/source identities; do not infer current support from historical155 evidence.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#39 N02](https://github.com/mckamyk/ardia/issues/39), [#41 N04](https://github.com/mckamyk/ardia/issues/41), [#17 F07](https://github.com/mckamyk/ardia/issues/17). These are not additional whole-issue blockers.

Scope: Matching pins are source metadata, not build or runtime evidence. Do not weaken the test/authentication gate, route Linux to Apple-only code, clean existing Chromium outputs, or reuse the155 binary as validation of156 APIs.

## N06 Linux shortcut hints and conflict resolution diverge from the actual native shortcut catalog

[#43 N06](https://github.com/mckamyk/ardia/issues/43) · P2 · new_confirmed_source_probe

Linux shortcut hints and conflict resolution diverge from the actual native shortcut catalog

### Current source evidence

The new shared resolver uses Mac catalog defaults for remappable items and only uses native defaults for fixed sources; all Linux items are remappable. Linux native Downloads defaults Ctrl+J, while shared catalog resolves Shift+command+J. Native tabSwitcher uses semantic command/physical Ctrl but shared catalog uses semantic control. Glyph formatting moved into state, outside Linux's components-only Vite translation.

- [apps/browser/src/state/commands/commandShortcuts.ts:32](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/commands/commandShortcuts.ts#L32)
- [apps/browser/src/state/commands/commandCatalog.ts:93](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/commands/commandCatalog.ts#L93)
- [apps/browser/src/state/commands/commandCatalog.ts:118](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/commands/commandCatalog.ts#L118)
- [apps/browser/src/state/commands/shortcuts.ts:13](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/commands/shortcuts.ts#L13)
- [apps/linux/src/shortcuts.ts:27](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shortcuts.ts#L27)
- [apps/linux/src/shortcuts.ts:81](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shortcuts.ts#L81)
- [apps/linux/src/shortcuts.ts:90](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shortcuts.ts#L90)
- [apps/linux/vite.config.ts:20](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/vite.config.ts#L20)

### Implementation slices

1. Make the shared effective resolver consume Linux catalog defaults/aliases and use one resolved catalog for display/conflicts/native binding.
2. Add Linux formatter injection instead of translating literal component source.

### Acceptance

- [ ] Downloads, DevTools, Ctrl+Tab and profile bindings display exactly their physical Linux defaults.
- [ ] Recording/reset/conflict displacement and open command/menu hints agree with native dispatch.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#29 F19](https://github.com/mckamyk/ardia/issues/29), [#37 F27](https://github.com/mckamyk/ardia/issues/37). These are not additional whole-issue blockers.

Scope: The build blocker N01 still prevents packaged UI reproduction on this revision, but this independent pure-source probe establishes resolver/native catalog disagreement. Linux uses semantic command for physical Ctrl and semantic control for physical Super.

## F02 The default isolated-profile flow and private windows are unsupported

[#12 F02](https://github.com/mckamyk/ardia/issues/12) · P1 · persists

A user can create and select an isolated profile whose real pages and services fail to load. New Incognito Window updates shell state but cannot create its native window.

### Current source evidence

The isolated-profile default still creates generated profile IDs; the Linux resolver permits only empty/default identities for native tabs, services and regular windows and rejects incognito. No upstream commit adds Linux profile ownership.

- [apps/browser/src/components/profiles/CreateProfile.tsx:41](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/profiles/CreateProfile.tsx#L41)
- [apps/browser/src/state/profiles/profiles.ts:79](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/profiles/profiles.ts#L79)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:271](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L271)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:738](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L738)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1218](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1218)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1961](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1961)

### Implementation slices

1. Add a Linux Profile/OTR registry and route tabs, services, windows and adoption through it.
2. Capability-gate unsupported profiles/private controls until implementation; never substitute the regular profile.
3. Reconcile failed native creation with shell state.

### Acceptance

- [ ] Independent profiles isolate cookies, history, localStorage and extensions; shared-data profiles share deliberately.
- [ ] Private windows create real OTR pages, clear on disposal and never persist.
- [ ] Cross-profile adoption rejects and failed creation leaves no phantom window.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#39 N02](https://github.com/mckamyk/ardia/issues/39), [#40 N03](https://github.com/mckamyk/ardia/issues/40), [#17 F07](https://github.com/mckamyk/ardia/issues/17), [#22 F12](https://github.com/mckamyk/ardia/issues/22), [#23 F13](https://github.com/mckamyk/ardia/issues/23). These are not additional whole-issue blockers.

Scope: Sharing default engine data can work as spaces. This is unsupported behavior, not demonstrated cross-profile leakage. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## F26 Unsupported platform choices are exposed as normal settings

[#36 F26](https://github.com/mckamyk/ardia/issues/36) · P2 · persists

Default-browser/autostart actions reject or fail silently. Choosing WebKit changes stored settings without selecting another engine. The app-drawn drag region does not initiate native window movement.

### Current source evidence

Default-browser/autostart still call absent ArdiaSystem methods with false/notRegistered fallbacks. WebKit remains selectable although Linux always creates Chromium; now protectedVideoInWebKit defaults true and automatically labels/routes streaming sites as WebKit. App drag region remains non-native. Incoming pageScrollBars adds another exposed preference: nativeSync calls absent ArdiaSystem.setThinScrollBars on startup and preference changes.

- [packages/shell/src/system.tsx:44](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/system.tsx#L44)
- [apps/browser/src/state/settings/paneData.ts:29](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/settings/paneData.ts#L29)
- [apps/browser/src/components/settings/panes/General.tsx:257](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/settings/panes/General.tsx#L257)
- [apps/browser/src/components/settings/panes/Engines.tsx:23](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/settings/panes/Engines.tsx#L23)
- [apps/browser/src/state/settings/settings.ts:171](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/settings/settings.ts#L171)
- [apps/browser/src/state/engines/engines.ts:58](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/engines/engines.ts#L58)
- [apps/linux/src/shims/web/HostWebView.tsx:63](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/web/HostWebView.tsx#L63)
- [apps/linux/src/shims/web/chrome.tsx:244](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/linux/src/shims/web/chrome.tsx#L244)
- [apps/browser/src/state/settings/settings.ts:61](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/settings/settings.ts#L61)
- [apps/browser/src/state/app/nativeSync.ts:70](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/app/nativeSync.ts#L70)
- [apps/browser/src/state/app/nativeSync.ts:129](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/app/nativeSync.ts#L129)
- [packages/shell/src/system.tsx:38](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/system.tsx#L38)
- [packages/shell/src/system.tsx:59](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/system.tsx#L59)

### Implementation slices

1. Add granular Linux capabilities and filter unavailable engines/platform settings.
2. Implement truthful default-browser/autostart query/change/error flows.
3. Disable unavailable automatic WebKit routing and add native custom-header move where supported.
4. Capability-gate or implement the Linux scrollbar preference through a supported engine/native path; handle unavailable operation without startup rejection.

### Acceptance

- [ ] No rendered choice calls an absent backend or reports an engine different from actual native host.
- [ ] URL routing/default registration and autostart survive restart.
- [ ] Streaming sites retain correct Chromium identity and header dragging works on target compositor.
- [ ] Toggle system/thin on a real page, restart, and verify actual page scrollbar behavior and no unhandled startup errors.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#41 N04](https://github.com/mckamyk/ardia/issues/41), [#43 N06](https://github.com/mckamyk/ardia/issues/43), [#12 F02](https://github.com/mckamyk/ardia/issues/12), [#18 F08](https://github.com/mckamyk/ardia/issues/18), [#24 F14](https://github.com/mckamyk/ardia/issues/24), [#46 R03](https://github.com/mckamyk/ardia/issues/46). These are not additional whole-issue blockers.

Scope: WebKit is not required on Linux; honest capability gating is. The new automatic protected-video route enlarges the existing F26 mismatch rather than proving DRM failure. Outer compositor decoration may supply dragging. Rebased source review at 8b165bd5; N01 prevents a fresh Linux UI from building, so prior native/adapter reproductions are historical evidence rather than a runtime result on this revision.

## Verification for implementation work

Use the current source baseline documented in the index. After N01–N05 restore runnable current-build acceptance, run Linux typecheck/build, focused domain and host fixtures, and actual native feature scenarios. Use physical compositor input for focus/drag/window claims and controlled service or scratch-profile fixtures for network, secrets, import and sync. Preserve native Chromium fallback behavior while testing Ardia’s own user controls.

Do not clean existing Chromium outputs or touch owner profiles. Preserve macOS/demo behavior, current source/API conventions and the no-comments rule for code. This planning document itself has no application tests to pass; a later implementation must record commands, results, current source/binary/UI identities and remaining acceptance requirements in its PR.
