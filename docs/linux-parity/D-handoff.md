# Lane D handoff: Linux UI-side gaps (F10, F26, F27)

Branch `t3/linux-ui-services`, based on `linux` at `4b3d21e0`. The changes are in `apps/linux`, plus one Linux-only native helper (`engine/ardiacore-linux`) for F10. Nothing in `apps/browser`, `packages/*`, `apps/demo` or macOS changed.

## F10 search suggestions ([#20](https://github.com/mckamyk/ardia/issues/20))

**The lane brief proposed widening the CSP. That would crash the shell, so this lane uses the plan's native request path instead.** In Chromium 156, a WebUI frame with bindings gets only the WebUI URL loader factory. `chrome://ardia` is not in `ChromeWebUIControllerFactory::IsWebUIAllowedToMakeNetworkRequests`. An https fetch that passed CSP would reach `WebUIURLLoaderFactory`, which calls `ReportBadMessage("Incorrect scheme")` and kills the shell renderer. Also, none of the nine suggestion providers send `Access-Control-Allow-Origin` (checked with an `Origin` header on 2026-10-27). `connect-src 'none'` stays, and a test pins it.

Contract:
- Host method `ArdiaSearch.suggest`, args `[url]`, is listed in the bootstrap `capabilities`.
  - Success reply: `{ body: string }`.
  - Refused request: `{code: "ERR_SUGGEST_REFUSED"}`.
  - Network, status or format failure: `{code: "ERR_SUGGEST"}`.
- `ac_linux_suggest_policy.h` (std-only) defines what passes:
  - The URL must be `<endpoint>?<query>` for one of the nine `suggestUrl` endpoints in `packages/core` `BUILT_IN_ENGINES`. It must be printable ASCII with no `#` or `\`, and at most 2048 bytes. The check runs on both the raw URL and the GURL-canonical spec.
  - The reply must be status 200, at most 64 KB, valid UTF-8 and a JSON array.
  - At most 6 fetches may run at once.
- `ac_linux_suggest.cc` makes the request:
  - It uses `SimpleURLLoader` on the regular profile's browser-process factory.
  - Credentials are omitted (no cookies), redirects are refused (`RedirectMode::kError`), the cache is disabled and the timeout is 5 s.
  - A `ProfileObserver` ends the fetch when the profile is destroyed. Replies are posted and reach `HostMessageHandler::Reply` through its weak pointer.
- In the UI, a Vite replacement for `packages/core/src/suggestFetcher.ts` (`apps/linux/src/suggestFetcher.ts`) keeps the shared fetcher's debounce, cache and generation checks, and swaps only its transport for `hostSuggestFetch`.
  - The transport refuses non-built-in URLs before reaching the host, so extension engines get no remote suggestions. It also refuses hosts that don't advertise the capability.
  - It rejects on abort and turns malformed or oversized replies into an empty failed response.
  - A JS abort does not cancel the native request; the in-flight cap bounds that.
- `ac_linux_host.cc` changes: one include, one dispatch block in `ShellHostView::Invoke`, and one capability entry. `BUILD.gn` adds the sources and `//services/network/public/cpp`.

## F26 platform settings ([#36](https://github.com/mckamyk/ardia/issues/36), partial)

- `hostCapabilities.ts` derives the Linux capabilities from the methods the host advertises:
  - default browser
  - login item
  - thin scrollbars
  - tracing
  - `openExternalURL`

  WebKit and CLI control are fixed as unavailable. Task Manager is available whenever a host exists (`engine.openTaskManager` is in the native contract).
- `resolveHostModule` (in `hostModules.ts`) now answers locally, without calling the host, when a method isn't advertised:
  - `isDefaultBrowser` and `setAsDefaultBrowser` reject with `ERR_UNAVAILABLE`.
  - `launchAtLoginStatus` returns `notFound`; `setLaunchAtLogin` rejects.
  - `setThinScrollBars` is a no-op. This removes nativeSync's startup rejection.
  - `ArdiaApp` intro music calls are no-ops, and `openExternalURL` returns `false`.

  A host that advertises a method is called normally.
- Vite adapters gate the UI without forking any pane:
  - The `settingsIndex` adapter removes the Engines pane from navigation, and removes unavailable rows from search.
  - The `Form` adapter's `Group`, `Row`, `Anchor` and `SectionHeader` drop unavailable anchors, and hide sections whose rows are all unavailable.
  - The `engines` adapter makes every engine decision Chromium: no rules, no streaming route, no engine mark, no switching. It also stops the DRM WebKit offer.
- Hidden rows: `general.defaultBrowser`, `general.openAtLogin`, `appearance.pageScrollBars`, `advanced.cliControl`, `advanced.cliInstall`, and every `engines.*` row.
- Still open:
  - Native default-browser registration (`shell_integration`), XDG autostart, thin page scrollbars and custom-header window dragging. Each needs native work.
  - The Site Controls "Opens in" menu still lists WebKit. Choosing it is refused by `openPageIn` and stores nothing.

## F27 app services ([#37](https://github.com/mckamyk/ardia/issues/37), partial)

Service-authority contract (`appServices.ts`):

| Scope | Services | Owner |
| --- | --- | --- |
| Renderer-local | The app commands addressed to the window's own `windowId`: `sendFeedback`, `copyDiagnostics`, `recordPerformanceIssue`, `showOnboarding`, `toolTour`, `videoTour`, `releaseNotes`, `taskManager`. Also `firstWindowCommitted`, once per renderer. | Every browser renderer. Utility renderers handle only `copyDiagnostics`, `taskManager` and `recordPerformanceIssue`. |
| App-wide, once per launch | `trackAppVersion`, onboarding start or resume, release notes after an update, and the default-browser re-offer when the host supports it. | A `startup` window that wins the Web Lock `ardia.linux.app-launch` (`ifAvailable`, held for the document's life). A `sessionStorage` mark stops a reloaded owner from running these again. Without Web Locks, nothing runs. |
| Profile-wide writers | Session, documents, sync and memory policy. | Unchanged; F04, F13 and F18 own them. |

- Linux keeps its session in `linux-session.json`, so the shared `session.json` probe would onboard every existing profile. Instead:
  - An existing profile (a saved session read at startup) with no `onboarding.json` is marked complete.
  - Onboarding targeted at another window is retargeted to the owner, which is the only renderer that draws it.
- Commands without a backend show a "Not available on Linux yet" toast: tracing, release-notes and video-tour URLs, and Task Manager failures. Send Feedback copies the report to the clipboard and says so.
- Deliberately not started: AppleScript, Handoff, CLI control, watched user scripts, telemetry, kill switches (the build has no updater), translation, sync (F13) and memory trim (F18).
- Still open:
  - Onboarding, tours, release notes and Send Feedback pressed in a utility window need a cross-renderer command route to a browser window.
  - The release-notes postcard appears only in the owner renderer's window.
  - Linux `systemInfo` has no `releaseNotesURL`, `videoTourURL` or `feedbackURL`.

## Tests run

- `pnpm --filter @ardia/linux test:host`: 230/230 pass. New files:
  - `suggestionCsp` (3)
  - `hostSuggestions` (7)
  - `hostCapabilities` (5)
  - `settingsCapabilities` (3)
  - `appServices` (10)
- `typecheck`, `build`, `test:graph` (now asserts the four new replacements) and `test:symbols` (4/4) pass.
- `pnpm --filter @ardia/linux fixture:csp:check` is a new headless runner for the existing fixture. It passes in `/usr/bin/chromium` with the policy parsed from `ac_linux_host.cc`, and checks:
  - Trusted Types
  - 13 of 13 direct fetches blocked
  - one host-bridged suggestion delivered
  - the startup window holds the launch lease
  - a second claimant gets `heldElsewhere`

  On `linux` this fixture could not start the UI: its mock was stale, and `setThinScrollBars` rejected at startup.
- `python3 -m unittest discover -s engine/ardiacore-linux/tests`: 120 run. The new `test_suggest_policy` passes. There is 1 failure plus 4 `setUpClass` errors, all in the Chromium close-patch fixtures (`unload_controller` patch does not apply to the bundled 156 fixture). The same failure reproduces on the `linux` base `4b3d21e0`; this lane did not touch those files.
- `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`: 7/7 pass.

## Needs native runtime acceptance on a Chromium 156 build

**None of the native code in this lane has been compiled.** The shared 156 output was mid-build and had no generated C++ mojom headers, so even a syntax-only check could not run. Lane rules forbid building. Check these:
- `ac_linux_suggest.cc` and the `ArdiaSearch.suggest` dispatch compile and pass `gn check`. The traffic annotation `ardia_linux_search_suggestions` may need to be registered if the auditor runs.
- Typing a non-URL query in the packaged UI with suggestions on returns real suggestions. With suggestions off, no request is made. The request carries no cookies, and redirects fail.
- Web Locks and `sessionStorage` work in `chrome://ardia`. On a fresh profile, onboarding starts once in the startup window. A restored second window and a renderer reload do not start it again. An existing profile is not onboarded.
- Settings shows no Engines pane, default-browser banner, login item, page-scrollbar choice or command-line section. Startup logs no `ArdiaSystem` or `ArdiaApp` rejections.
