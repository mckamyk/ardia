# Lane I handoff: site permissions, data clearing and the content blocker (#16 F06)

Branch `t3/linux-site-controls`, rebased onto `linux` at `4fd0946e` (after #62, #63 and #68). Source-level implementation, tracked by #94. Runtime acceptance stays with #16 and the #81 suite. Nothing here was built into Chromium 156 or run natively: I could not build Chromium on this machine, and the 156 output tree lacks the generated extension mojom headers needed for even a syntax-only compile of the service.

## What was done

Before this lane, `sites.*` reached the engine through the generic route table with no argument checks. `profiles.clearBrowsingData` and every `contentBlocker.*` call answered `ERR_UNKNOWN_CALL`, so Settings showed a null blocker and the site panel's blocker toggle did nothing.

### Native host

- **Intercept.** `ac_linux_host.cc` checks `LinuxSiteControlsHandles(name)` in `engine.call` right after #62's profile-name gate and `profiles.release`, and before #63's zoom intercept, the route table and registry resolution. It hands the call to `LinuxSiteControlsCall` with the window's app profile. The `sites.*` routes are removed from `ac_linux_contract.h`, and `AppendLinuxSiteControlsCapabilities` adds the owned calls to the bootstrap capability list, which #68 builds from the route table. The remaining hunks:
  - `ShellHostView::Initialize` calls `StartLinuxContentBlocker(profile())`, using the app profile.
  - `StartLinuxHost` calls `StartLinuxSiteControls(...)`, which sets the restricted-component hook and the change sink. The sink sends `contentBlocker.changed` to every window whose app profile matches.
- **Pure helper.** `ac_linux_site_controls.{h,cc}` needs only `base/values.h`. It holds:
  - which calls the lane owns;
  - the profile decision, `ResolveProfile`;
  - strict validation of origins, setting types and values, browsing-data types and `since`, blocker hosts and list ids;
  - engine-reply mapping;
  - the blocker state normaliser, ported from `ArdiaContentBlocker.mm`, including its `Category` mapping;
  - the per-profile `BlockerQueue`: tickets, a 20 s deadline plus 2 s grace, 40 retries 250 ms apart, the retriable-error list from `ArdiaContentBlockerCalls.mm`, a 30 s idle close and a cap of 64 queued calls.
- **Service.** `ac_linux_site_controls_service.{h,cc}`:
  - `SiteControlsProfile` is the one function that picks the profile; see Profile resolution below.
  - Site and browsing-data calls run the existing `ac_site_settings_get/set/origins/reset`, `ac_site_data_clear` and `ac_browsing_data_clear` services through `ac_engine_call`, with the resolved profile's path.
  - `LinuxContentBlocker` exists once per regular profile and observes `ProfileObserver`. On start it inspects `extensions/ublock` beside the executable, or `--ardia-content-blocker-dir`, off the UI thread. After `ExtensionSystem::ready()` it loads that directory as a component extension through `ComponentLoader::AddOrReplace`, exactly as `ardiacore_profile.mm` does. If the id it gets back isn't `fajbdaomknpkicfjcpipdmcpoamdmilh`, or that extension isn't then in `enabled_extensions()` at that path (`AddOrReplace` returns the manifest's id even when creating the extension fails), it removes the registration and reports the blocker as failed. Calls queued before the load finishes have the same 20 s native deadline as every other call.
  - Calls run in a hidden `WebContents`: view type `kTabContents`, with the extension WebContents observer attached, showing `chrome-extension://fajbdaomknpkicfjcpipdmcpoamdmilh/ardia-linux-bridge.html`.
  - Chromium `CHECK`s against main-world `ExecuteJavaScript` on extension pages. The browser therefore sets a `data-ardia-request` attribute from `ISOLATED_WORLD_ID_CHROME_INTERNAL` and polls `data-ardia-reply` every 25 ms.
  - Every script runs only while the committed URL and origin are the bridge page's. Renderer loss, a failed load or a navigation elsewhere resets the page and retries. A reset never destroys the page inside its own observer notification: the page is retired and deleted in a posted task, or synchronously when the profile goes away. Late callbacks are discarded by a page generation counter.
  - A successful mutation queues a state read and broadcasts its `stats` as `contentBlocker.changed`.
- **Bridge page.** `engine/ardiacore-linux/content-blocker/ardia-linux-bridge.{html,js}`:
  - It loads uBlock's own `js/vapi.js`, `vapi-common.js` and `vapi-client.js`, then the bridge.
  - The bridge ports the macOS prelude: waiting for readiness, `settlePending`, `applyLists` with the `ardiaReloadPending` marker, `covers`, the off directive and the dev fail-next flag.
  - It exposes only five fixed operations: `state`, `setEnabled`, `setList`, `isAllowed` and `setAllowed`. Nothing is evaluated from the request.
- **Restricted component.** `g_ardia_restricted_component_extension` is set in every Linux process: `chromium-ardia-linux-layer.patch` assigns it first thing in `ChromeMainDelegate::BasicStartupComplete`, which runs in the browser and the zygote that renderers fork from, and `StartLinuxSiteControls` sets the same predicate again in the browser. uBlock's component status therefore grants no script-everywhere access in any process: not to `chrome://` pages including the shell, not to the Chrome Web Store, and not to other extensions' frames. That's the same hook macOS sets from its main delegate. The patch's extension id is checked against `kExtensionId` by `test_site_controls.py`.

### Packaging

- `scripts/linux/fetch-ubo.sh` runs the unchanged `packages/ardiacore/scripts/ubo.sh`, with `flock` and `sha256sum` standing in for `lockf` and `shasum`. The result is the same pinned, patched uBlock Origin 1.75.0 build macOS embeds.
- `scripts/linux/package.py --content-blocker <dir>` copies it to `lib/ardia/extensions/ublock` without `_metadata` and adds the bridge files. It refuses a build that has no manifest key, isn't MV2, or lacks the messaging scripts. Without the option the package has no blocker, and the host says so.

## Contract

| Call | Arguments (exact) | Reply | Errors |
| --- | --- | --- | --- |
| `sites.settings` | `{origin, engine?: "chromium"}` | `SiteSettings`, known types only | `ERR_SITE_REQUEST`, `ERR_UNKNOWN_CALL` (other engines), `ERR_ENGINE`, `ERR_BAD_REPLY` |
| `sites.setSetting` | `{origin, type, value, engine?}` | `null` | as above |
| `sites.origins` | `{}` | `string[]`, valid http(s) origins only | |
| `sites.reset` / `sites.clearData` | `{origin}` | `null` / `{cookies: number \| false, storage: boolean}` | |
| `profiles.clearBrowsingData` | `{types: 1–4 unique of history, siteData, cache, downloads; since?: null \| ms ≥ 0}` | `null` once the engine finishes | `ERR_CLEAR_INCOMPLETE` when a type fails |
| `contentBlocker.state` | `{}` | `ContentBlockerState`. When not installed, not loaded, timed out or the profile is gone: `{enabled: false, lists: [], allowedHosts: [], stats: {ready: false, error}}`, the macOS shape | |
| `contentBlocker.setEnabled` | `{enabled}` | `null` | `ERR_CONTENT_BLOCKER_UNAVAILABLE` with the bridge's message, `ERR_CONTENT_BLOCKER_REQUEST`, `ERR_CONTENT_BLOCKER_BUSY` |
| `contentBlocker.setListEnabled` | `{id: [A-Za-z0-9._-]{1,128}, enabled}` | `null` | as above |
| `contentBlocker.isAllowed` / `setAllowed` | `{host}` / `{host, allowed}`, host-name syntax only, lowercased | `boolean` / `null` | as above |
| `contentBlocker.devFailNextMessage` | `{}` | `null`; the next call fails with `… failed (dev)` | |
| `contentBlocker.devDelayNextLoad` | any | | `ERR_CONTENT_BLOCKER_UNAVAILABLE` |
| event `ArdiaContentBlocker`/`contentBlocker` | `{name: "contentBlocker.changed", data: ContentBlockerStats}` | | |

- **Validation.** Origins must be `http(s)://host[:port]` with no path, user, query or fragment, at most 2048 bytes. The host must be lowercase labels, an IPv4 address or a bracketed IPv6 address. A host that contains `/` (the off directive), `#`, `*` or spaces is refused.
- **Profile resolution.** `SiteControlsProfile` is the single function that picks the profile. It now uses #62's registry.
  - Every call first passes the host's `ParseProfileName` gate, so malformed ids are `ERR_PROFILE_INVALID`.
  - `sites.*` and `profiles.clearBrowsingData`: an `incognito:` id is `ERR_PROFILE_PRIVATE` before any lookup. Other ids go through `ResolveLinuxProfile`, so `""`/`default` and isolated profiles each get their own settings and data. An off-the-record result is `ERR_PROFILE_PRIVATE`, and registry failures keep their codes (`ERR_PROFILE_DELETED`, `ERR_PROFILE_UNAVAILABLE`, …). Private windows never fall back to the regular profile.
  - `contentBlocker.*` is app-level, as on macOS (`kProfile = ""`). Whatever the window's profile, private or isolated, it acts on the app profile's blocker. Since #108, isolated profiles load their own blocker and follow the app profile's settings; see [S-handoff.md](S-handoff.md).
- **Not in this lane.** `sites.zoomLevels` and `sites.setZoom` belong to the page-state lane (#63, `PrepareLinuxZoomDomain`). The contract defines no cancellation or progress for clearing. Cancelling is the shared dialog's Cancel, which the acceptance scenario covers.

## Tests run

All runs are on this branch after the rebase onto `6a791ee0` and the review fixes. The full native suite was not rerun for the fix round; `test_site_controls.py` was.

- `python3 -m unittest discover -s engine/ardiacore-linux/tests`: 343 tests, OK. That includes 8 new tests in `test_site_controls.py`:
  - The pure helper fixture, under ASan and UBSan: profile decisions, origin edge cases, a permission grant/revoke/reset round trip against a fake settings map, scoped clears, blocker requests and states, the bridge scripts and the queue state machine.
  - The real service compiled against stub Chromium interfaces:
    - site-setting round trips through `ac_engine_call`;
    - registry resolution: an isolated profile's calls carry its own path, deleted and unknown profiles keep the registry's codes, private ids and off-the-record results are refused without reaching the engine, and blocker calls never consult the registry;
    - component load, including the wrong-id case;
    - restricted-hook behaviour;
    - bridge URL, view type and observer;
    - renderer crash and retry;
    - dropped reply and timeout;
    - not-ready retries;
    - foreign navigation, with no request sent to it;
    - the dev fail-next flag;
    - idle close;
    - the 64-call cap;
    - profile destruction with 64 calls pending and before load.
  - The bridge under node against a simulated uBlock dashboard (8 cases).
  - Wiring checks on the host, GN and the restricted hook.
- `pnpm --filter @ardia/linux test:host`: 320 passed, including 4 new in `hostSiteControls.test.mjs`. They drive the shared `@ardia/ardiacore` functions through the Linux module resolver and the real bootstrap transport.
- `pnpm --filter @ardia/linux typecheck` and `pnpm --filter @ardia/linux build`: pass.
- `pnpm --filter @ardia/linux acceptance:self-test`: 51 passed. That includes the new stock-Chromium 155 test: the helper's exact send and poll scripts, read from `ac_linux_site_controls.cc`, drive the real bridge page from an isolated world, with an MV3 stand-in for uBlock's messaging.
- `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`: 7 passed.
- `python3 -m unittest discover -s engine/patches/build/tests`: OK, including the new packaging test.
- I also ran the shims on this machine: `fetch-ubo.sh` with its paths redirected to `/tmp` downloaded, verified and patched uBlock 1.75.0. I checked that build's `vapi-client.js`, its privileged-origin rule and the bundled EasyList rule the scenario relies on.
- `python3 -m unittest discover -s engine/chromium/src/chrome/browser/ardia/tests`: 9 of 10 pass. The failure, `test_linux_excludes_apple_and_mac_host`, flags the `ardia/core` include in `ac_downloads.cc`. That file isn't touched here, and the test fails the same way on `linux`.
- Nothing shared under `apps/browser` or `packages/` changed, so the browser suite wasn't rerun.

## Needs native runtime acceptance on a Chromium 156 build

These can only be checked on a built engine:

- **Build.** The host and service compile and link, and `gn check` passes for the new `//chrome/browser/extensions` dependency (`ComponentLoader`).
- **Restrictions.** `ExtensionsBrowserClient::CreateExtensionWebContentsObserver` and `SetViewType` on a hidden `WebContents` are enough for uBlock's `runtime.connect` from the bridge page to be privileged (`PRIVILEGED_ORIGIN`).
- **Component load.** uBlock MV2 loads as a component extension in the Helium 156 tree, including under the suite's `--disable-extensions`, which normally still allows component extensions.
- **The F06 scenario** (`--only F06`) against a package built with `--content-blocker`:
  - permissions seen by the page after reload and restart;
  - per-site and `since`-scoped clears;
  - the dialog's Cancel and Clear Data;
  - an EasyList-matched image never reaching the fixture server;
  - the exception, restart, Block Again, off/on and EasyList off/on.
- **Manual:**
  - The site panel and Privacy settings show the same state.
  - Real-device permission prompts work.

## Follow-ups and open risks

- **#106 Navigation hold.** For non-default profiles, and for the default profile while Lite settings are pending, macOS holds navigations until uBlock's rulesets are in force. Linux has no hold, so requests made in the first seconds after the first launch may go unfiltered. This matches macOS for the default profile.
- **#107 Restricted-component hook in child processes.** Fixed at source level by the `BasicStartupComplete` hunk above. Still needs the native check that a component content script can't run on `chrome://ardia/` or the Web Store.
- **#108 Blocker in isolated profiles.** Isolated-profile windows load no blocker, because blocker calls are app-level. Until #108, calls from a window whose page profile isn't the app profile (or its off-the-record child) answer the not-ready state with `the content blocker isn't available in this profile yet`, or `ERR_CONTENT_BLOCKER_UNAVAILABLE`, instead of the app profile's settings. macOS loads uBlock into every regular profile, and `FollowDefault` keeps each one on the default profile's lists and exceptions.
- **Timeouts.** The bootstrap rejects any native request after 30 s. A blocker call queued behind several slow ones can time out in JavaScript while it still runs natively. Its reply is then dropped and the next state read shows the true state.
- **Blocking file IO on the UI thread.** `ComponentLoader::AddOrReplace` reads the manifest on the UI thread, as on macOS. Thread restrictions only check this in DCHECK builds.
- **Parsing assumption.** The scenario's permission checks rely on headless Chromium answering `navigator.permissions.query` for camera from content settings without a device. Calibrate this on the first run (#91).
