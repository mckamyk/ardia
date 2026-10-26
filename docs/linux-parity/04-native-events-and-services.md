# Linux page events and native browser services

Draft planning layer 4/6. This workstream describes repairs owned by [#13 F03](https://github.com/mckamyk/ardia/issues/13), [#16 F06](https://github.com/mckamyk/ardia/issues/16), [#17 F07](https://github.com/mckamyk/ardia/issues/17), [#18 F08](https://github.com/mckamyk/ardia/issues/18), [#19 F09](https://github.com/mckamyk/ardia/issues/19), [#25 F15](https://github.com/mckamyk/ardia/issues/25), [#26 F16](https://github.com/mckamyk/ardia/issues/26), [#27 F17](https://github.com/mckamyk/ardia/issues/27), [#34 F24](https://github.com/mckamyk/ardia/issues/34), [#35 F25](https://github.com/mckamyk/ardia/issues/35). It adds no application behavior and closes no repair issue. The Git base chain is document order; use the [coordination tracker](https://github.com/mckamyk/ardia/issues/10) and [index](README.md) for actual blockers and parallel lanes.

## Integration contract

Suggested role: **Page observer and domain service owners**. File boundary: Separate Linux-native helpers by security, downloads, extensions, media, page messaging and credentials. One host dispatcher/observer integrator. Shared packages remain contracts; secret native APIs retain typed paths.

- Security/media/capture/fullscreen/page-focus event coverage.
- Typed secure storage and reauthentication.
- Bounded page-message substrate.
- Download/extension/filesystem view ownership.

Use separate service/page helper owners and one native host integrator. Native Chromium fallback is not Ardia integration. Complete the public callbacks and current service envelope; preserve automatic PiP while adding manual controls. Secret operations remain typed, native and authenticated. Only the bounded page-message substrate is a prerequisite for scroll/selection consumers; native theme-color, security, downloads and extension management can proceed independently.

## F03 HTTP and HTTPS pages can be described as “Local Page”

[#13 F03](https://github.com/mckamyk/ardia/issues/13) · P1 · persists

Ardia’s site panel gives incorrect connection information and cannot show the expected certificate or mixed-content details.

### Current source evidence

SiteControls still asks getSecurityInfo; Linux has no command or security event producer. Security remains null, which SiteConnection converts to Local Page and local/internal description for an ordinary HTTP/HTTPS page.

- [apps/browser/src/components/site/SiteControls.tsx:136](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/site/SiteControls.tsx#L136)
- [apps/browser/src/components/site/SiteConnection.tsx:9](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/site/SiteConnection.tsx#L9)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1434](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1434)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2153](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2153)

### Implementation slices

1. Observe native security state and implement getSecurityInfo with the current public schema.
2. Render unknown/unavailable honestly and invalidate old certificate state on navigation.

### Acceptance

- [ ] Check HTTPS, HTTP, mixed content, invalid certificate and internal pages.
- [ ] Unknown never renders local and previous-document certificates cannot persist.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#27 F17](https://github.com/mckamyk/ardia/issues/27), [#39 N02](https://github.com/mckamyk/ardia/issues/39). These are not additional whole-issue blockers.

Scope: This is incorrect Ardia presentation, not evidence of a TLS validation bypass. Runtime recheck waits on bootstrap migration.

## F06 Site permissions, data clearing and Ardia content-blocker controls are disconnected

[#16 F06](https://github.com/mckamyk/ardia/issues/16) · P2 · persists

The shared site panel and Privacy settings cannot reliably read, change or reset permissions, clear browsing data, manage origin exceptions or configure Ardia’s filter lists.

### Current source evidence

Current APIs use sites.*, profiles.clearBrowsingData and contentBlocker.* domain calls. Linux implements neither the new domain dispatch nor underlying missing site/blocker Expo equivalents. Settings still catches unavailable resources as empty origins/exceptions or null blocker.

- [packages/ardiacore/src/siteSettings.ts:14](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/siteSettings.ts#L14)
- [packages/ardiacore/src/contentBlocker.ts:34](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/contentBlocker.ts#L34)
- [packages/ardiacore/src/module.ts:145](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/module.ts#L145)
- [apps/browser/src/state/settings/paneData.ts:36](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/settings/paneData.ts#L36)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2074](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2074)

### Implementation slices

1. After N02, map typed per-profile site settings/data clearing/zoom operations to supported services.
2. Implement Linux blocker backend and expose explicit unavailable/error states.
3. Coordinate destructive confirmation UX with F05 without blocking non-dialog service work.

### Acceptance

- [ ] Grant/revoke real permissions; verify actual engine state after reload/restart.
- [ ] Clear fixture cookies/storage/cache/history with scope and cancellation checks.
- [ ] Toggle filter lists/site exception and verify network behavior rather than successful dispatch only.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#15 F05](https://github.com/mckamyk/ardia/issues/15), [#13 F03](https://github.com/mckamyk/ardia/issues/13), [#17 F07](https://github.com/mckamyk/ardia/issues/17), [#33 F23](https://github.com/mckamyk/ardia/issues/33). These are not additional whole-issue blockers.

Scope: Chromium enforcement/native permission bubbles may work independently. N02 migration can restore transport but is not itself a Linux blocker implementation.

## F07 Passwords and Autofill management have no usable native bridge

[#17 F07](https://github.com/mckamyk/ardia/issues/17) · P2 · persists

The Passwords pane cannot unlock/manage/export saved credentials, and addresses/cards can appear empty or autofill enabled even though their data could not be read.

### Current source evidence

Non-secret management now uses passwords.* and autofill.* domain calls; secret writes deliberately use dedicated ArdiaPasswords.save/update and ArdiaAutofill.saveCard. Linux has no implementations. Pane resources still hide unavailable state as enabled defaults/empty collections. Linux ReauthAvailable remains false.

- [packages/ardiacore/src/passwords.ts:6](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/passwords.ts#L6)
- [packages/ardiacore/src/autofill.ts:32](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/autofill.ts#L32)
- [packages/ardiacore/src/nativeModules.ts:17](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/nativeModules.ts#L17)
- [apps/browser/src/state/settings/paneData.ts:40](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/settings/paneData.ts#L40)
- [engine/chromium/src/chrome/browser/ardia/ac_reauth_linux.cc:12](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/chromium/src/chrome/browser/ardia/ac_reauth_linux.cc#L12)

### Implementation slices

1. Wire metadata/config and address CRUD via typed domain handlers.
2. Implement secure native storage and real authentication before unlock/reveal/export.
3. Preserve dedicated secret-bearing native methods and report unavailable distinctly from empty vault.

### Acceptance

- [ ] Fixture credential/address/card management, enabled toggles and restart work.
- [ ] Authentication denied/cancelled/expired never reveals secrets; inspect encryption at rest.
- [ ] No passwords/full card numbers in generic request JSON, logs or telemetry.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#12 F02](https://github.com/mckamyk/ardia/issues/12), [#15 F05](https://github.com/mckamyk/ardia/issues/15), [#21 F11](https://github.com/mckamyk/ardia/issues/21), [#22 F12](https://github.com/mckamyk/ardia/issues/22), [#23 F13](https://github.com/mckamyk/ardia/issues/23). These are not additional whole-issue blockers.

Scope: Native Chromium save/fill is separate. Keyring encryption/passkeys are not established. Secret operations must not fall back to generic JSON, logs, unprotected storage or bypass authentication.

## F08 Ardia’s Downloads view lacks download events and controls

[#18 F08](https://github.com/mckamyk/ardia/issues/18) · P2 · persists

Downloads can occur in Chromium without appearing in Ardia’s popover or internal Downloads page; pause/resume/cancel and file open/reveal are not wired through the Linux shell.

### Current source evidence

Downloads now subscribes to downloads.changed through ArdiaDownloads and calls downloads.cancel/pause/resume/settings. Linux still emits legacy ArdiaCEF.onEngineEvent and has no current domain handlers, download initialization/translation or file open/reveal backend. Unlike the old baseline, settings also cannot be claimed currently working until N02.

- [packages/ardiacore/src/module.ts:129](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/module.ts#L129)
- [packages/ardiacore/src/module.ts:135](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/module.ts#L135)
- [apps/browser/src/state/app/nativeSync.ts:66](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/app/nativeSync.ts#L66)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1181](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1181)
- [packages/shell/src/system.tsx:50](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/shell/src/system.tsx#L50)

### Implementation slices

1. Initialize native download observation/snapshot and deliver current domain event schema.
2. Wire control methods and reconcile native IDs with persisted records.
3. Add shared Linux file open/reveal/folder-picker primitives with F12/F26 coordination.

### Acceptance

- [ ] Known file shows progress/completion in Ctrl+J and pause/resume/cancel work.
- [ ] Restart has no duplicate or stale live entry; open/reveal target selected file.
- [ ] Download destination and ask-where settings alter actual engine behavior.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

Blocks full acceptance of: [#30 F20](https://github.com/mckamyk/ardia/issues/30).

Coordinate contracts/shared files with [#22 F12](https://github.com/mckamyk/ardia/issues/22), [#30 F20](https://github.com/mckamyk/ardia/issues/30), [#36 F26](https://github.com/mckamyk/ardia/issues/36). These are not additional whole-issue blockers.

Scope: Actual Chromium downloading is separate and may work. Source revalidation does not establish native download failure.

## F09 Extensions can be listed, but management and toolbar integration are incomplete

[#19 F09](https://github.com/mckamyk/ardia/issues/19) · P2 · persists

Enable/disable, install/reload/uninstall, action popups, side panels and extension search-engine discovery cannot operate through the shared Ardia UI.

### Current source evidence

Shared extension APIs now use extensions.* and search.engines calls and domain events. Linux retains legacy ArdiaExtensions.list, but current listExtensions calls ArdiaExtensions.call(list,...), so even listing regresses until N02; management/action/panel implementations remain absent afterward.

- [packages/ardiacore/src/extensions.ts:114](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/extensions.ts#L114)
- [packages/ardiacore/src/extensions.ts:118](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/extensions.ts#L118)
- [packages/ardiacore/src/extensions.ts:152](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/extensions.ts#L152)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2054](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2054)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2074](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2074)

### Implementation slices

1. Restore listing through current typed domain contract then implement management and registry events.
2. Implement action states/execution, real popup/side-panel hosting semantics and install consent.
3. Map search.engines and installation picker without unrestricted forwarding.

### Acceptance

- [ ] Fixture extension install/consent/list/toggle/reload/uninstall works.
- [ ] Action badge/popup/side panel target correct tab/profile; search provider updates and removes.
- [ ] Handle cancelled install, increased permissions and native view lifecycle.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#27 F17](https://github.com/mckamyk/ardia/issues/27), [#37 F27](https://github.com/mckamyk/ardia/issues/37). These are not additional whole-issue blockers.

Scope: An installed extension may execute in native Chromium. Listing support exists only in the legacy native contract, not current UI. Native messaging/1Password require separate evidence.

## F15 Shared media controls and manual PiP are missing their command/event paths

[#25 F15](https://github.com/mckamyk/ardia/issues/25) · P2 · persists

Playing-audio badges, sidebar/mini-player transport and camera/microphone/screen indicators do not receive their expected state. Ardia’s manual PiP toggle and media-command controls reject.

### Current source evidence

Linux still lacks media/nowPlaying/mediaAccess producers and manual media/PiP commands. Shared startup now calls webView.setDisplayMediaPicker/setAutoPictureInPicture/setVideoUpscaling unconditionally; the old ArdiaCEF auto-PiP implementation is unreachable until N02. Cast/capture controls also lack current handlers.

- [apps/browser/src/components/window/TabWebView.tsx:195](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/window/TabWebView.tsx#L195)
- [apps/browser/src/state/media/pip.ts:23](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/media/pip.ts#L23)
- [packages/ardiacore/src/module.ts:120](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/module.ts#L120)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1688](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1688)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2153](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2153)

### Implementation slices

1. Add media/audibility/session/capture events and native transport/manual PiP commands with user-activation validation.
2. Restore global auto-PiP setting under webView domain without altering tested native cancellation lifecycle.
3. Implement or honestly gate cast/source controls.

### Acceptance

- [ ] Audio badges/player metadata and transport update correct real tab.
- [ ] Manual and automatic PiP pass separate entry/return/dismissal tests.
- [ ] Real portal sharing reports capture state and stop/change-source accurately.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#28 F18](https://github.com/mckamyk/ardia/issues/28), [#27 F17](https://github.com/mckamyk/ardia/issues/27). These are not additional whole-issue blockers.

Scope: Existing native automatic PiP code/events remain present, but current UI integration needs domain migration before claiming it works. Native playback/mute/page-requested PiP/portal sharing are distinct fallbacks.

## F16 Peek/split-link gestures and selection tools do not reach native pages

[#26 F16](https://github.com/mckamyk/ardia/issues/26) · P2 · persists

Configured link modifiers, selection search/popovers, quote/highlight links, jump-to-selection and find/replace-in-field lack the page-side integration used by macOS.

### Current source evidence

Peek/selection settings now call webView.* domain methods rather than optional ArdiaCEF methods. Linux still lacks page-script/channel installation, command/pageMessage producers, evaluate and executeJavaScript. Updating transport alone will not implement the page tools.

- [packages/ardiacore/src/module.ts:123](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/module.ts#L123)
- [apps/browser/src/state/peek/actions.ts:102](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/peek/actions.ts#L102)
- [apps/browser/src/state/site/selection.ts:79](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/site/selection.ts#L79)
- [apps/browser/src/components/window/TabWebView.tsx:224](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/window/TabWebView.tsx#L224)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1434](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1434)

### Implementation slices

1. Add bounded page-message/request-response substrate with frame/document lifetime checks.
2. Install shared page script appropriately and forward supported modifiers/settings.
3. Implement selection/peek tools while preserving ordinary/download/post-body/native link behavior.

### Acceptance

- [ ] Real-page modifier peek/split, selection search/quote/jump/replace and Escape work.
- [ ] Navigation invalidates pending replies; cross-origin/iframe spoof attempts cannot execute privileged operations.
- [ ] No __ardiaHost in ordinary tabs.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#35 F25](https://github.com/mckamyk/ardia/issues/35), [#28 F18](https://github.com/mckamyk/ardia/issues/28), [#44 R01](https://github.com/mckamyk/ardia/issues/44). These are not additional whole-issue blockers.

Scope: Store-driven split creation/layout remains distinct. Ordinary pages must never obtain privileged shell bridge; incoming page messages are untrusted.

## F17 Fullscreen, renderer failures and several page-status events never reach Ardia

[#27 F17](https://github.com/mckamyk/ardia/issues/27) · P2 · persists

Ardia cannot reliably hide its chrome for page fullscreen, show its own crash/unresponsive recovery state, show hovered-link status or surface blocked-pop-up state.

### Current source evidence

Linux observer still produces only navigation/progress/title/error/favicon/open-window/destruction and PiP. Fullscreen/crashed/unresponsive/responsive/status/popupBlocked are not produced. Shared callbacks moved to TabWebView and public page-event schema now uses name/data, with find/focus special names.

- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:464](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L464)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2153](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2153)
- [apps/browser/src/components/window/TabWebView.tsx:203](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/window/TabWebView.tsx#L203)
- [packages/ardiacore/src/pageEvents.ts:59](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/pageEvents.ts#L59)

### Implementation slices

1. Add native observers and initial/rebind/navigation state delivery.
2. Implement associated recovery/popup commands and reject stale events.
3. Coordinate one event envelope with F03/F15/F23/F25 and window bookkeeping R03.

### Acceptance

- [ ] Fullscreen hides/restores correct Ardia chrome.
- [ ] Terminate/hang a scratch renderer and recover; hovered links clear status; blocked popup can be reviewed/opened.
- [ ] Repeat after split/transfer/navigation/destruction without stale state.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#13 F03](https://github.com/mckamyk/ardia/issues/13), [#25 F15](https://github.com/mckamyk/ardia/issues/25), [#33 F23](https://github.com/mckamyk/ardia/issues/33), [#44 R01](https://github.com/mckamyk/ardia/issues/44), [#46 R03](https://github.com/mckamyk/ardia/issues/46). These are not additional whole-issue blockers.

Scope: Chromium fullscreen, popup enforcement and native crash UI may remain functional; the issue is shared Ardia chrome/recovery state. Direct host-specific callback transport can remain if it meets public schema.

## F24 New favicon refreshes can remain generic after a cache miss

[#34 F24](https://github.com/mckamyk/ardia/issues/34) · P2 · persists

Newly visited or changed sites can keep generic icons in the sidebar, essentials and tab strip.

### Current source evidence

Favicon events still call unsupported per-tab downloadFavicon. Fallback now calls favicons.fetch domain API with no Linux implementation. Cached get/set also moved to favicons domain, so existing cache service availability depends on N02; the original refresh gap survives transport repair.

- [apps/browser/src/state/favicons/favicons.ts:126](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/favicons/favicons.ts#L126)
- [apps/browser/src/state/favicons/favicons.ts:139](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/state/favicons/favicons.ts#L139)
- [packages/ardiacore/src/favicons.ts:6](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/favicons.ts#L6)
- [packages/ardiacore/src/favicons.ts:19](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/packages/ardiacore/src/favicons.ts#L19)
- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:1434](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L1434)

### Implementation slices

1. Map cached favicons.get/set then implement bounded native fetch/download result contracts.
2. Handle navigation cancellation, candidate changes, image decoding/size limits and stale tab results.

### Acceptance

- [ ] Uncached controlled favicon appears in row/essentials/top strip, changes, and survives restart.
- [ ] Malformed/oversized/unreachable images fall back without stale repaint or persistent error.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#39 N02](https://github.com/mckamyk/ardia/issues/39). These are not additional whole-issue blockers.

Scope: Do not claim all artwork broken: persisted data URLs/predefined art can render. Frequency not screenshot-verified on rebased UI.

## F25 Website tint and scroll-driven toolbar hiding cannot respond to pages

[#35 F25](https://github.com/mckamyk/ardia/issues/35) · P2 · persists

“Extend Website Color” does not receive site theme color, and “Hide Toolbar While Scrolling” lacks the scroll events it needs.

### Current source evidence

Linux Navigation still omits themeColor/themeColorSource. Shared TabWebView expects theme state and pageMessage(scroll) for toolbar auto-hide; native host installs no producer. Settings still exposes both behaviors.

- [engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc:2135](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc#L2135)
- [apps/browser/src/components/window/TabWebView.tsx:230](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/window/TabWebView.tsx#L230)
- [apps/browser/src/components/settings/panes/Tabs.tsx:85](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/settings/panes/Tabs.tsx#L85)
- [apps/browser/src/components/settings/panes/Tabs.tsx:117](https://github.com/mckamyk/ardia/blob/6907d6831980c3b6d686ccba0d3abdb8be2d5ebe/apps/browser/src/components/settings/panes/Tabs.tsx#L117)

### Implementation slices

1. Report native theme color/source and clear state correctly on navigation.
2. Deliver bounded scroll reports through F16 substrate and synchronize setting changes.

### Acceptance

- [ ] Controlled theme-color page updates tint and resets across navigation.
- [ ] Scroll down/up hides/reveals toolbar; disabling either setting restores normal behavior.
- [ ] Background/iframe/stale-document events cannot control another tab.

### Dependencies and parallel handoff

No issue-wide hard blocker. Work can start now after agreeing the contracts below.

No issue-wide downstream block is declared.

Coordinate contracts/shared files with [#26 F16](https://github.com/mckamyk/ardia/issues/26), [#27 F17](https://github.com/mckamyk/ardia/issues/27). These are not additional whole-issue blockers.

Scope: Different shaders/fonts/blur are expected rendering adaptations and excluded. Theme reporting is independent of full page-script implementation; scroll-hide depends only on its narrow message substrate.

## Verification for implementation work

Use the current source baseline documented in the index. After N01–N05 restore runnable current-build acceptance, run Linux typecheck/build, focused domain and host fixtures, and actual native feature scenarios. Use physical compositor input for focus/drag/window claims and controlled service or scratch-profile fixtures for network, secrets, import and sync. Preserve native Chromium fallback behavior while testing Ardia’s own user controls.

Do not clean existing Chromium outputs or touch owner profiles. Preserve macOS/demo behavior, current source/API conventions and the no-comments rule for code. This planning document itself has no application tests to pass; a later implementation must record commands, results, current source/binary/UI identities and remaining acceptance requirements in its PR.
