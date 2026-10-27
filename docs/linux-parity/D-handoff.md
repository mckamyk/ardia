# Lane D handoff: Linux UI-side gaps (F10, F26, F27)

Branch `t3/linux-ui-services`, rebased onto `linux` at `2376f370` (#61, #64, #90 and #65 merged). The changes are in `apps/linux`, plus one Linux-only native helper (`engine/ardiacore-linux`) for F10. Nothing in `apps/browser`, `packages/*`, `apps/demo` or macOS changed.

## F10 search suggestions ([#20](https://github.com/mckamyk/ardia/issues/20))

**The lane brief proposed widening the CSP. That would crash the shell, so this lane uses the plan's native request path instead.** In Chromium 156, a WebUI frame with bindings gets only the WebUI URL loader factory. `chrome://ardia` is not in `ChromeWebUIControllerFactory::IsWebUIAllowedToMakeNetworkRequests`. An https fetch that passed CSP would reach `WebUIURLLoaderFactory`, which calls `ReportBadMessage("Incorrect scheme")` and kills the shell renderer. Also, none of the nine suggestion providers send `Access-Control-Allow-Origin` (checked with an `Origin` header on 2026-10-27). `connect-src 'none'` stays, and a test pins it.

Contract:
- Host methods `ArdiaSearch.suggest` (args `[url, id]`) and `ArdiaSearch.cancelSuggest` (args `[id]`). Both are listed in the bootstrap `capabilities`.
  - Success reply: `{ body: string }`.
  - Refused request: `{code: "ERR_SUGGEST_REFUSED"}`.
  - Network, status or format failure, or a superseded or cancelled request: `{code: "ERR_SUGGEST"}`.
- `ac_linux_suggest_policy.h` (std-only, fixture-tested) defines what passes:
  - The URL must be `<endpoint>?<query>` for one of the nine `suggestUrl` endpoints in `packages/core` `BUILT_IN_ENGINES`. It must be printable ASCII with no `#` or `\`, and at most 2048 bytes. The check runs on both the raw URL and the GURL-canonical spec.
  - The request id must be 1 to 64 characters of `[A-Za-z0-9_-]`.
  - The reply must be status 200, at most 64 KB, valid UTF-8 and a JSON array.
- `ac_linux_suggest.cc` makes the request:
  - Each window's `ShellHostView` owns a `SuggestionClient`. A window keeps at most 4 fetches; a new request evicts the oldest instead of being refused, and a repeated id replaces its predecessor.
  - Evicted, cancelled and finished fetches are destroyed, which cancels their `SimpleURLLoader`, and their replies are posted to the weak handler callback. Closing a window destroys all of its fetches.
  - Requests use the regular profile's browser-process factory. Credentials are omitted (no cookies), redirects are refused (`RedirectMode::kError`), the cache is disabled and the timeout is 5 s. A `ProfileObserver` ends a fetch when the profile is destroyed.
- Native gate for "suggestions off": `session.write` wraps its reply callback (`NoteSuggestionsOnWrite`). Only a successful merge and write records a setting, and what it records is the merged session it replies with. The setting is `settings.searchSuggestions`, keyed by the original profile's path; a missing key means the shared default (on). Requests are refused while the setting is off or not yet known. The startup window always writes the session before it renders, so the value is known before the first request. The wrap is a separate statement before the `session.write` branch, because native fixtures compile that branch on their own.
- The traffic annotation `ardia_linux_search_suggestions` declares `user_data: USER_CONTENT`, `internal.contacts.owners: "//ardia/linux/OWNERS"` and `last_reviewed`. That OWNERS file contains only `*`, because Ardia has no named owners. **Owner decision:** replace the contact with a real team address if you have one.
- In the UI, a Vite replacement for `packages/core/src/suggestFetcher.ts` (`apps/linux/src/suggestFetcher.ts`) keeps the shared fetcher's debounce, cache and generation checks, and swaps only its transport for `hostSuggestFetch`.
  - The transport gives each request its own id. When the shared fetcher aborts a request still in flight, it sends `cancelSuggest` (only if the host advertises it).
  - It refuses URLs that are not `<built-in endpoint>?…` before reaching the host, and refuses hosts without the capability.
  - It turns malformed or oversized replies into an empty failed response.
- Extension search engines: an extension engine whose `suggestionsUrl` starts with a built-in endpoint does pass, but only to that endpoint and still without cookies. Other extension suggestion URLs get no remote suggestions.
- Client certificates: `credentials_mode = kOmit` drops cookies and HTTP auth, but Chromium may still offer a client certificate on the TLS handshake unless the `kOmitCorsClientCert` feature is enabled. It is off by default and this lane does not change it.
- Private windows: the gate accepts off-the-record profiles, as the acceptance suite expects. A private window fetches on its own storage partition, cookies are still omitted, and the original profile's setting gates it. The host still attaches no off-the-record windows; that work belongs to F02. A future private-window shell that runs on the regular profile would need its own native gating, because its requests would be indistinguishable.
- `ac_linux_host.cc` changes:
  - includes;
  - a `suggestions_` member;
  - the `suggest` and `cancelSuggest` dispatch blocks;
  - one `NoteSuggestionsSetting` line in the `session.write` reply;
  - capability entries.

  `BUILD.gn` adds the sources and `//services/network/public/cpp`.

## F26 platform settings ([#36](https://github.com/mckamyk/ardia/issues/36), partial)

- `hostCapabilities.ts` derives the Linux capabilities from what the host advertises: default browser, login item, thin scrollbars, `openExternalURL`, tracing and Task Manager. WebKit and CLI control are fixed as unavailable.
  - The bootstrap now appends every `kRoutes` name from `ac_linux_contract.h` to `capabilities`, so the native call contract is the source of truth at runtime.
  - Tracing needs `engine.beginTracing`, `engine.endTracing` and `engine.isTracing`, none of which the contract has yet. Task Manager needs `engine.openTaskManager`, which it has.
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
| App-wide, once per process | These steps, in order: `version` (`trackAppVersion`, recording `versionUpdated`), `onboarding`, `releaseNotes` and `defaultBrowserOffer`. | Whichever browser window holds the Web Lock `ardia.linux.app-launch`. Every browser window queues for it in waiting mode. The native host records completed steps per process and profile (`ac_linux_launch_steps.h` via `ArdiaShell.launchSteps` / `ArdiaShell.completeLaunchStep`). The holder runs only unrecorded steps and records each before the next. When it releases the lock, by finishing or because its window closed, the next window continues the rest. Everyone after that finds every step done. Reloads are covered by the native record. A failed step releases the lock so the next window retries. Without Web Locks or the native record, nothing runs. |
| Profile-wide writers | Session, documents, sync and memory policy. | Unchanged; F04, F13 and F18 own them. |

- Fresh-profile evidence: a browser window whose profile has no saved session awaits `linux-fresh-profile.json` before its first session flush. The marker is removed once a later launch finds `onboarding.json` on disk. The onboarding decision reads `onboarding.json` and the marker from disk, not from the bootstrap snapshot. `ArdiaShell.readDocument` returns null only for a missing file (`ENOENT`); permission errors, symlinks, directories and oversized files reject with `ERR_DOCUMENT_READ`, which counts as "unknown":
  - **Record present:** the shared flow (resume, or nothing if finished).
  - **Record absent, marker present (or no saved session):** start onboarding.
  - **Record absent, no marker, saved session:** a profile from before this change, so mark onboarding complete. This cannot overwrite anything, because the record is confirmed absent on disk.
  - **Record on disk but missing from the snapshot (128-document / 8 MB cap), or any read failure:** do nothing, because the shared code would write over it.
- Remaining edge: if the marker write itself fails while the first session flush succeeds and the onboarding write is lost, the next launch treats the profile as pre-existing.
- Onboarding that `maybeStartOnboarding` targets at another window is retargeted to the lock holder, the only renderer that draws it.
- A takeover window calls `trackAppVersion` again. The version is already saved, so this only restores the pending postcard in its own renderer.
  - Release notes are due when `versionUpdated` was recorded, or when `version` was not recorded and this run saw an update.
  - Remaining case: `trackAppVersion` computes and saves the version in one shared call. A holder that dies after that save but before recording `versionUpdated` loses the release-notes page. The postcard still shows from the saved pending entry.
- The renderer that runs launch steps sets `sessionStorage["ardia.linux.app-launch"] = "ran"`. This is evidence only, for the acceptance suite; nothing reads it.
- Commands without a backend show a "Not available on Linux yet" toast: tracing, release-notes and video-tour URLs, and Task Manager failures. Send Feedback copies the report to the clipboard and says so.
- Deliberately not started: AppleScript, Handoff, CLI control, watched user scripts, telemetry, kill switches (the build has no updater), translation, sync (F13) and memory trim (F18).
- Still open:
  - Onboarding, tours, release notes and Send Feedback pressed in a utility window need a cross-renderer command route to a browser window.
  - The release-notes postcard appears only in the lock holder's window.
  - Linux `systemInfo` has no `releaseNotesURL`, `videoTourURL` or `feedbackURL`.

## Tests run

Review round 3, on `2376f370`:
- `pnpm --filter @ardia/linux test:host`: 278/278 pass, including the host tests from #61, #64 and #65.
- `typecheck`, `build`, `test:graph`, `test:symbols` (4/4) and `acceptance:self-test` (47/47) pass.
- `pnpm --filter @ardia/linux fixture:csp:check` passes in headless `/usr/bin/chromium` under the policy parsed from `ac_linux_host.cc`. It checks:
  - Trusted Types
  - 13 of 13 direct fetches blocked
  - one host-bridged suggestion delivered
  - all four launch steps recorded
  - a second window gets `alreadyDone`
- `python3 -m unittest discover -s engine/ardiacore-linux/tests`: 291/291 pass. The suite includes:
  - this lane's `test_suggest_policy` and `test_launch_steps`;
  - `test_host_policy` (stored-read classification);
  - the fixtures that compile host branches: `test_document_host`, `test_session_merge`, `test_window_storage`, `test_window_lifecycle` and `test_downloads`.
- `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`: 7/7 pass.

## Needs native runtime acceptance on a Chromium 156 build

**None of the native code in this lane has been compiled.** The shared 156 output still has no generated C++ mojom headers, so even a syntax-only check could not run, and lane rules forbid building. Check these:
- `ac_linux_suggest.{h,cc}`, `ac_linux_launch_steps.h` and the new host hunks compile and pass `gn check`. The traffic annotation `ardia_linux_search_suggestions` passes the auditor if it runs.
- Suggestions in the packaged UI:
  - Typing a non-URL query with suggestions on returns real suggestions.
  - With suggestions off, no request is made, and the native gate refuses one forced through.
  - Requests carry no cookies, and redirects fail.
  - Fast typing on a throttled network always shows suggestions for the newest query, and a second window is not starved.
- Launch services:
  - Web Locks work in `chrome://ardia`.
  - On a fresh profile, onboarding starts once.
  - Closing the startup window mid-launch lets a restored window finish the remaining steps.
  - A renderer reload or a later window runs nothing.
  - A second launch after a killed first launch still onboards.
  - A profile from before this change is not onboarded.
- Settings shows no Engines pane, default-browser banner, login item, page-scrollbar choice or command-line section. Startup logs no `ArdiaSystem` or `ArdiaApp` rejections. Task Manager works.
