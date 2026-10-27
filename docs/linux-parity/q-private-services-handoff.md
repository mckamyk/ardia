# Lane Q handoff: private-window services and profile follow-ups

Issues: #88, #72, #71, #89, #101, #73 and #99, tracked at source level by #142. Branch `t3/linux-private-services`, rebased on `linux` at `980389e1`. None of this has run in a Chromium 156 build; see the last section.

## What changed

### #88 Private-window site settings and zoom

- `ac_linux_private_services.h` (header only, no Chromium dependencies) holds the calls that serve a private session. These are the same seven engine calls macOS serves (`PrivateSessionPolicy` in `ArdiaEngineBridge.mm`):
  - `sites.settings`, `sites.setSetting`, `sites.origins`, `sites.reset` and `sites.clearData`;
  - `sites.zoomLevels` and `sites.setZoom`.
- `RoutePrivateCall` decides for a private name:
  - a call that isn't on the table → `ERR_PROFILE_PRIVATE`;
  - a page profile that isn't off the record → `ERR_PROFILE_WINDOW`;
  - a session whose shutdown has started → `ERR_PROFILE_ENDING`;
  - a window that doesn't host the name → `ERR_PROFILE_WINDOW`.
- A served call goes to the original profile's path. The host adds `{"offTheRecord": true}` itself; the renderer still can't send that key. The engine then resolves the primary OTR profile and refuses one whose shutdown has started.
- Two engine errors get their own codes, in the host's `Replied` and in the site-controls reply: `no private session` → `ERR_PROFILE_ENDING`, and `not for a private session` → `ERR_PROFILE_PRIVATE`.
- History, bookmarks, favicons, download settings, `profiles.clearBrowsingData` and extension management keep refusing private names. Chrome keeps no such data for an OTR profile.
- A regular window naming a private session now gets `ERR_PROFILE_WINDOW` instead of `ERR_PROFILE_PRIVATE`. F06's acceptance check was updated to match.
- Also fixed: `ac_linux_site_controls_service.cc` was missing `base/one_shot_event.h` and did not compile against the 156 headers.

### #73 Automatic PiP for every profile

- `ac_linux_auto_pip.h` keeps the last preference the app sent. `ac_linux_auto_pip_service.cc` is a `ProfileManagerObserver`. It writes the `AUTO_PICTURE_IN_PICTURE` default into every loaded regular profile, and into each profile added later.
- Chrome calls `OnProfileAdded` before it runs the `CreateProfileAsync` callback that opens the isolated window, so that window already has the setting.
- Nothing is written until the app has sent the preference, and only regular profiles get it.
- Private windows don't inherit an ALLOW. `AUTO_PICTURE_IN_PICTURE` is `INHERIT_IF_LESS_PERMISSIVE`, so with the preference on a private window reads ASK; with it off it inherits BLOCK, and the global switch blocks every window anyway. Chromium ignores default-setting writes on OTR profiles, so ALLOW can't be set there. macOS writes only regular profiles too and also reads ASK in private windows. macOS gates entry on ALLOW, though, so it never auto-enters PiP in a private window, while on Linux Chrome's helper enters and asks inside the PiP window. That gap is #173.

### #101 Page commands in isolated and private windows

`LinuxPageCommands` now makes two separate checks:

- **Ownership.** The Browser and the contents are on the page profile, the tab is in that Browser's strip, and the window isn't DevTools. A failure is still `ERR_PAGE_STALE_TARGET`.
- **Profile kind.** Any regular profile and the primary OTR profile are served. Guest, system and non-primary OTR profiles get `ERR_PAGE_PROFILE`.

Save Page and DevTools follow Chrome's incognito behaviour through the checks that were already there: file-selection dialogs, `DownloadRestrictions`, content restrictions, and `AllowDevToolsFor` on the page profile. Chrome itself doesn't refuse either command in incognito.

### #72 Private downloads

- **Routing.** `linux_downloads::RouteDownloads` routes download calls by the window's page profile:
  - in a private window, a call with no profile name, or naming that window's own session, uses the OTR profile's `DownloadManager`;
  - an isolated or foreign private name → `ERR_PROFILE_WINDOW`;
  - a closing session → `ERR_PROFILE_ENDING`.
- **List.** The engine's list for the original profile is filtered to `offTheRecord` entries, and then to GUIDs the window's own OTR manager holds.
- **Controls.** Cancel, pause, resume and keep are checked against the window's own manager before the engine call. This also keeps a regular window from controlling a private download: the engine's lookup spans every OTR profile of the original. An isolated window's controls now work too; before, they resolved the default profile.
- **Events.** An OTR `downloads.changed` reaches only private windows whose OTR manager owns that GUID. It carries the original profile's name, which is how macOS labels it, so the shared `downloadVisibleIn` shows it in private windows of that profile only. Removal events still reach every window, including the ones the engine sends when the OTR profile is destroyed.
- **Files.** File operations (status, open, reveal, trash, icon, drag and drop) follow the page profile into private windows.
- **Adapter.** `hostDownloads.ts` lists only OTR entries in a private window and labels them with the original profile.
- **Persistence.** Chrome never writes OTR downloads to history, and the Linux adapter keeps no downloads document.

### #71 Open Link in Incognito Window

- `chromium-linux-private-links.patch` adds a Linux-only hook to `NavigateImpl`. It sits after `AdjustNavigateParamsForURL`, so URLs that can't open in incognito are already back in a regular tab, and before any Browser or contents is created.
- The hook takes only browser-initiated navigations with a user gesture. A compromised renderer that sends `OFF_THE_RECORD` therefore can't open an Ardia private window. Chrome's context-menu entry builds its params with `CreateBrowserInitiated` and a gesture, so it qualifies.
- The host takes an `OFF_THE_RECORD` navigation when the source is one of its tabs with a live page channel. It sends that page `openWindow` with disposition `incognito` and the source window's original profile. The shared `openFromPage` then opens an Ardia private window.
- Other sources keep Chrome's behaviour: extension popups, side panels and DevTools (#150).
- The patch applies to the 156 tree and compiles there with the navigator target's flags. Both were checked on a copy.

### Review round 1: content blocker in private windows

Content-blocker calls route to the app profile, so `setAllowed`, `setEnabled` and `setListEnabled` from a private window, or naming a private session, wrote the regular profile's uBlock state. `PrivateBlockerRefusal` now refuses them with `ERR_PROFILE_PRIVATE`. Reads still answer, so a private window's site panel shows the regular rules it is filtered by; changing them takes a regular window.

### #89 Private windows in other windows' lists

- **Directory.** `ArdiaShell.privateWindows` returns the other private windows as bare ids, with no title, URL, tab or profile. Every renderer gets an `ArdiaShell` `privateWindows` event when a private window attaches or closes.
- **Access.** `ArdiaShell.focusWindow` accepts a private window from any live renderer (`WindowFocusableBy`). Frame, title, minimise, maximise, close and `windowIds` keep F02's self-or-opener rule. `windowIds` still hides private windows, so the startup sweep can't close them.
- **Adapter.** `hostPrivateWindows.ts` labels the entries "Private Window", "Private Window 2" and so on. It drops stale or failed answers and focuses a window when it's chosen.
- **Not done.** Linux has no Window menu: `setMenuState` is a no-op. Wiring this into one is #148.

### #99 Extensions in private windows

- **Window calls.** `actionStates` and `sidePanelUrl` serve a private window's OTR page profile. A closing session gets `ERR_PROFILE_ENDING`.
- **Incognito access.** Lookups from an OTR tab skip extensions without incognito access (`extensions::util::IsIncognitoEnabled`), because the OTR `ExtensionRegistry` redirects to the full set. This covers action state, side-panel options and running an action.
- **Views.** `extensionView.create` accepts the window's own private session. It checks incognito access before calling `ExtensionViewHostFactory`, whose incognito path `CHECK`s it. Chrome then hosts the view in the OTR profile (split mode) or the original (spanning mode). A disallowed extension gets `ERR_EXTENSION_PRIVATE`.
- **Change events.** Extension list changes also reach private windows of that profile.
- **Still refused.** Management calls and install-prompt answers from private names. Chrome manages extensions from regular windows, and OTR install prompts use Chrome's own dialog.
- **Not done.** The shared toolbar shows only split-mode extensions in private windows. Live action updates for OTR tabs aren't delivered. Both are #149.

## Contract summary

| Call | Private window, its own session | Regular window naming a private session |
| --- | --- | --- |
| `sites.settings/setSetting/origins/reset/clearData`, `sites.zoomLevels/setZoom` | OTR profile via the engine's `offTheRecord` call; `ERR_PROFILE_ENDING` while closing | `ERR_PROFILE_WINDOW` |
| `history.*`, `bookmarks.*`, `favicons.*`, `profiles.clearBrowsingData`, extension management | `ERR_PROFILE_PRIVATE` | `ERR_PROFILE_PRIVATE` (site controls: `ERR_PROFILE_WINDOW` first) |
| `contentBlocker.setAllowed/setEnabled/setListEnabled` | `ERR_PROFILE_PRIVATE` (the blocker lives in the regular profile) | `ERR_PROFILE_PRIVATE` |
| `contentBlocker.state/isAllowed` | Answered from the app profile's blocker | Answered |
| `downloads.list/remove/clear/cancel/pause/resume/keep` | The OTR `DownloadManager`, own GUIDs only | `ERR_PROFILE_WINDOW` |
| Find, Print, Save Page, DevTools | Served (Chrome's policies apply) | n/a |
| `extensions.actionStates/sidePanelUrl`, `extensionView.create` | Served for incognito-allowed extensions; `ERR_EXTENSION_PRIVATE` otherwise | `ERR_PROFILE_WINDOW` for views |
| `ArdiaShell.privateWindows`, `ArdiaShell.focusWindow` | Lists the other private windows | Lists all private windows by id; focus allowed; nothing else |

## Tests

Full runs on `linux` at `6bf5097b`, plus review round 1 on `980389e1`. Round 1 re-ran the touched native tests (106 passed across the 16 touched modules, and 25 patch-tooling tests), `test:host` (376 passed), the acceptance self-test (63 passed; its stock-Chromium extension test flaked once under load and then passed), `typecheck` and `build`:

| Command | Result |
| --- | --- |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | 434 run, 434 passed |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 8 passed |
| `pnpm --filter @ardia/linux test:host` | 371 passed (adds `hostPrivateWindows.test.mjs` and new cases in `hostDownloads` and `hostTabPage`) |
| `pnpm --filter @ardia/linux acceptance:self-test` | 63 passed. In an earlier run under machine load the stock-Chromium F09 badge test failed once; it passed alone twice and in the final run, and this lane doesn't touch it. |
| `pnpm --filter @ardia/linux typecheck`, `build`, `test:graph` | Pass (graph 3/3) |
| `clang++ -fsyntax-only` with the 156 headers and the `ardia/linux` flags | The new and changed helpers compile, as does the patched `browser_navigator.cc` with the navigator target's flags. `ac_linux_extension_service.cc` shows the same 3 errors before and after this lane. `ac_linux_host.cc` showed the same 6 errors before and after each hunk until #136 landed (156 API drift such as `base/containers/contains.h` and `contents_web_view`, owned by the build thread); since #136 it stops at the generated `ardia/linux/mojom/page.mojom.h`, which that out dir doesn't have yet, so the host's final hunks were checked only by the source tests. |

These were written to fail first, and each passes after its fix:

- **New tests.** `test_private_services.py`, which also checks the served table against macOS and checks that each served engine call is declared `OffTheRecord::kAllow`. Also `test_private_downloads.py`, `test_private_links.py`, `test_private_windows.py` and `test_auto_pip_profiles.py`.
- **Fixture scenarios.**
  - `private_services_scenarios.cc` and `auto_pip_preference_scenarios.cc` are new;
  - `site_controls_service_scenarios.cc`, `downloads_scenarios.cc`, `extension_service_scenarios.cc` and `extension_views_scenarios.cc` gained private-session cases;
  - `test_page_commands`'s authority mode, `test_profiles`'s window access and the extension broadcast test changed with the new rules.

## Acceptance scenarios

`apps/linux/scripts/acceptance/scenarios/private-services.mjs` adds F02-88, F02-72, F02-71, F02-89, F02-101, F02-73 and F02-99 (see `acceptance-suite.md`). Run them with `--only 88,72,71,89,101,73,99`.

- #71 is manual: background instances only log Chrome's context menu.
- #89's menu entry waits for #148.

## Needs native runtime acceptance on a Chromium 156 build

I couldn't build or run Chromium 156 in this lane, so nothing here has run in a browser. On a 156 build:

1. Run the seven scenarios above, plus F02, F06, F08 and F09 to catch regressions.
2. Check these by hand:
   - Open Link in Incognito Window from a regular and an isolated window;
   - the private window's Downloads popover;
   - Print and Save Page in isolated and private windows;
   - a split-mode extension's toolbar button, popup and side panel in a private window.
3. Look for `CHECK` failures in the browser log while opening extension popups in private windows.

## Follow-ups

- #148: a Linux Window menu listing regular and private windows.
- #149: spanning-mode extensions and live action updates in private windows.
- #150: incognito links from extension popups, side panels and DevTools.
- #172: `downloads.removed` for private downloads still reaches every window (ids only).
- #173: automatic PiP in private windows asks instead of matching macOS.
