# F02 handoff: isolated profiles and private windows on Linux

Issue: [#12 F02](https://github.com/mckamyk/ardia/issues/12). Branch `t3/linux-profiles-private`, based on `linux` at `4b3d21e0`.

## What changed

Linux had no profile authority. The host accepted only the empty or `default` engine profile for tabs, services and windows, and rejected private windows. This branch adds a profile registry, routes the host through it, and changes the Linux UI so that a profile it can't host shows an explicit failure instead of falling back to the regular profile.

### Native

- `engine/ardiacore-linux/src/ardia/linux/ac_linux_profiles.{h,cc}`: the registry, with no Chromium dependencies. It parses engine profile IDs, resolves them through a pluggable backend, combines concurrent loads into one, maps private names, tracks released and deleted IDs, and forgets destroyed profiles.
- `ac_linux_profile_service.{h,cc}`: the Chromium backend. Isolated profiles load with `ProfileManager::CreateProfileAsync` from `<user data>/Profile <id>`. That is the sibling of the app profile and the same directory naming macOS uses. Private names resolve to the original profile's primary OTR profile. A `ProfileObserver` drops every registry entry before Chrome destroys the profile.
- `ac_linux_host.cc` (narrow hunks):
  - Each native window now has two profiles: an app profile (the shell WebUI, documents, session) and a page profile (the Chrome `Browser` profile, which owns every tab in the window). `profile()` is still the app profile. The new `page_profile()` is used for tab creation, launch claims, adoption, transfer and viewports.
  - `ArdiaShell.openWindow` validates the profile and `incognito` flag together. It binds a private name to its own window ID and resolves the profile asynchronously, then creates the Chrome window in that profile. Failure returns a structured `ERR_PROFILE_*` error. An extra request for an ID already being opened is merged into the first one.
  - `HostRegistry::Attach` adopts only windows that Ardia requested in a non-default profile, after checking that the Browser profile still matches the request. A mismatched window is closed. Chrome-made OTR or foreign-profile windows are still not adopted.
  - Domain calls resolve their `profile` argument. Services for isolated profiles get that profile's path. Private profiles get `ERR_PROFILE_PRIVATE` instead of the regular profile's data, because an OTR profile shares its parent's path. `profiles.release` ends a private name. `documents.*` stays app-level and accepts only the default profile.
  - Engine events map a profile path to its engine ID (`""` or the isolated ID). Off-the-record events are still dropped.
  - Strips and `windows.list` report each window's page profile. The bootstrap carries `pageProfile`, and `privateWindow` for private windows.
  - When a private window's Browser closes, every other renderer gets `ArdiaShell.onWindowEvent {type: "close"}`. The shared window sync then removes the window and releases its private name.
- `ac_linux_session.cc`: `ValidLinuxSession` rejects any session that carries private data. That covers `incognito:` IDs in `profileId`/`lastProfileId` fields, `profiles`/`parkedPins`/`pinChoices`/`pinSync` keys or `profileOrder`, plus any window flagged `incognito` or carrying `originalProfileId`. Writes, merges and departures therefore fail closed instead of persisting private state.

### Contract

| Engine profile ID | Meaning | Resolves to |
| --- | --- | --- |
| `""` or `default` | Default | The app profile (the first attached Browser's profile) |
| `<letter>[A-Za-z0-9_-]{0,127}`, not `default` in any case | Isolated | `<user data>/Profile <id>`, loaded or created on demand. Never the default profile, never OTR, never another ID's profile. |
| `incognito:<window>@<original>` | Private | The primary OTR profile of `<original>`. `<window>` must equal the window ID it opens. |

Anything else is `ERR_PROFILE_INVALID`. Other errors: `ERR_PROFILE_DELETED`, `ERR_PROFILE_RELEASED` (a private name never comes back after release), `ERR_PROFILE_UNAVAILABLE`, `ERR_PROFILE_WINDOW` (a tab asks for a profile its window doesn't host, or a window ID that is still opening is requested with a different profile), `ERR_PROFILE_PRIVATE`, and `ERR_PROFILE_ENDING` (the original's previous private session is still being destroyed after 5 s of retries).

There is no fallback anywhere. A tab, adoption or transfer works only when the resolved profile is the window's own page profile, and adoption across profiles is rejected.

Private windows that share one original profile share Chrome's primary OTR profile, as on macOS and in Chrome's own incognito. Chrome destroys that profile, discarding its data, when its last browser closes.

### Linux UI

- `apps/linux/src/hostWindowProfiles.ts`:
  - `createHostWindowOpener` replaces the fire-and-forget `ArdiaShell.openWindow`. If native creation fails, it emits `onWindowEvent {type: "close"}` for that window, so the shared store drops it and releases its private name. No phantom window remains. A private window always sends its own state as `privateWindow`. Without that state it fails rather than opening a regular window.
  - `withPrivateWindow` builds a private renderer's initial session from that seed (only that window and its own tabs). A private bootstrap without a seed fails at startup.
  - `installProfileWindows` keeps each regular native window on one engine profile:
    - Switching to a profile that shares the window's data switches in place.
    - Switching to an isolated profile focuses an existing window of that profile, or opens one.
    - Moving a tab to an isolated profile reopens its URL in that profile's open window. Without one, it opens a new window and closes the source tab only after the opener confirms that window was created (`windowOpened`), so a failed open keeps the tab.
- `hostSession.ts`, `main.tsx` and `shims/shellBase.tsx` only register these. A private window doesn't adopt the default profile's parked pins.

## Review round 1

| Finding | Fix | Failing-before test |
| --- | --- | --- |
| A new or reloaded renderer's startup `sweep()` closed private windows it didn't know | `ArdiaShell.windowIds` lists a private window only to that window (`WindowVisibleTo`). `requestWindowClose` and `closeWindow` on a private window are accepted only from the window itself or its opener (`WindowClosableBy`, with the opener recorded at `openWindow`). | `test_utility_close…test_private_window_close_requests_only_come_from_the_window_or_its_opener` (runs the real `requestWindowClose` branch; a stranger closes the window without the gate), `test_profiles` `private_window_access`, `test_profile_host…test_private_windows_are_hidden_from_and_protected_against_other_renderers` |
| Reopening a private window while the last private session is still being destroyed | Chrome calls `OnProfileWillBeDestroyed` immediately, then keeps the dying OTR profile registered with its original until its renderer hosts exit, and `GetPrimaryOTRProfile(true)` hands that same profile back. The registry now inspects the existing OTR profile first. If its shutdown has started, it retries every 100 ms for up to 50 ticks, then fails with `ERR_PROFILE_ENDING`. `Watch` never observes a profile whose shutdown has started. The opener reconciles the failure, so no phantom window remains. | `test_profiles` `reopen_after_ending` / `ending_bounded` (the fake now returns the same dying profile, as Chrome does), `test_profile_host…test_dying_profiles_are_never_observed_and_private_creation_waits_for_them` |
| Focusing an isolated window made it Chrome's last-used profile; the next launch used it (or the profile picker) as the app profile | Once the app profile is attached, local-state pref observers keep `profile.last_used` on the app profile's directory and `profile.last_active_profiles` limited to it. They also turn off `kBrowserShowProfilePickerOnStartup` (when it isn't policy-managed), because with two profiles in storage Chrome 156 shows the picker at launch by default. Isolated and private windows also set `omit_from_session_restore`. | `test_profiles` `startup_pinned`, `test_profile_host…test_app_profile_stays_the_startup_profile` |
| Attach refusing a mismatched window closed it with no close event | `RefuseRequestedWindow` drops the request, broadcasts `onWindowEvent {type: "close"}` to every renderer, then closes the Chrome window. | `test_profile_host…test_refused_window_is_reported_closed_to_every_renderer_before_chrome_closes_it` (compiles the real functions), `…test_attach_refusal_and_private_close_use_the_broadcast` |
| A cross-profile tab move closed the source before its target window existed | The move reuses that profile's open window. Otherwise the source tab closes only after the new window opened. | `hostProfiles.test.mjs`: "keeps the tab when that profile's window cannot open", "closes the source only after its window opens", "reuses that profile's open window" |
| A duplicate `openWindow` for an ID still opening reported success, even for another profile or a load that never returns | `OpeningLedger` ties each pending opening to its profile. A duplicate with the same profile joins the pending result, and one with another profile gets `ERR_PROFILE_WINDOW`. Requests already created or attached also check their profile. Every opening expires after 30 s with `ERR_PROFILE_UNAVAILABLE`, and a profile that resolves late after expiry never creates a window. | `test_profiles` `opening_ledger`, `test_profile_host…test_window_openings_are_profile_bound_and_bounded` |

Other regular windows still don't learn about private windows, for example in window menus. The coordinator is tracking that.

## Integration with #61 and #64

- Window lifecycle (#61): a new window still goes through the profile-bound opening ledger and the async profile resolution. It is shown with #61's `ShowNativeWindow(created, WindowFocus().Create(...))`, and its requested frame comes from `RequestedFrame`; both travel in `WindowOpening`. The reuse branches keep #61's `WindowFocus().Reuse` activation. A request that hasn't attached yet must also ask for the same profile. Private windows receive #61's focus, frame and fullScreen broadcasts like any window. Renderers that don't hold the window ignore them, because `setFocusedWindow` returns early for unknown IDs.
- Downloads and files (#64): `downloads.*` calls and `ArdiaSystem`/`ArdiaFile*` file methods use the window's page profile, so an isolated window lists and clears its own downloads. A request naming another profile gets `ERR_PROFILE_WINDOW`. Private windows get `ERR_PROFILE_PRIVATE`; private downloads are tracked in #72. The Linux downloads adapter labels listed downloads with the window's `pageProfile` so the shared `downloadVisibleIn` shows them in that window. The shell WebView is #64's `LinuxShellWebView`, created on the app profile.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest engine/ardiacore-linux/tests/test_profiles.py test_profile_host.py` | 2 + 6 passed. The compiled registry fixture covers parsing, lookup, no fallback, aliasing and OTR-as-regular rejection, private lifetime, disposal, release and delete during load, reopening while a private session ends (bounded), reentrancy, registry destruction, startup profile pinning, private window access and the opening ledger. |
| `python3 -m unittest engine/ardiacore-linux/tests/test_session_merge.py` | 2 passed, including the new private-session rejections. They fail when the check is reverted. |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | 215 run, 215 passed, on `linux` with #67. The `test_launch_claims` fakes model the window's hosted profile. The shared `document_host_fixture` fake host now has `access()` and can share one app profile across hosts, so close-dispatch tests can send requests between windows. |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed |
| `pnpm --filter @ardia/linux test:host` | 210 passed, including 8 in `hostProfiles.test.mjs`. The private-bootstrap case fails without the session hook. |
| `pnpm --filter @ardia/linux typecheck`, `build`, `test:graph` | Pass |
| `clang++ -fsyntax-only` against the 156 tree headers and build flags | `ac_linux_profiles.cc`, `ac_linux_profile_service.cc` and `ac_linux_session.cc` compile. `ac_linux_host.cc` could not be checked: the 156 output tree was mid-build and had no generated mojom headers yet. Its first compile happens in the next incremental 156 build. |

## Needs native runtime acceptance on a Chromium 156 build

None of this has run in a live Chromium 156 build. I couldn't build one in this lane. Check:

1. Create an isolated profile and switch to it. A new window opens. Its cookies, history, localStorage and extensions are separate from the default profile's. A shared-data profile switches in place and shares data.
2. New private window, from a regular window and from an isolated-profile window. Its pages are OTR, nothing from it appears in `session.json`, and its data is gone once all private windows of that original are closed. After a restart, no private window comes back.
3. Adoption or transfer of a tab between windows of different profiles is rejected. Stop or deny profile creation (for example with a read-only user data dir): the window disappears from the sidebar and window list instead of staying behind.
4. Settings panes for an isolated profile (history, passwords, site settings) reach that profile's services.

## Known limits and follow-ups

- Regular windows hold one engine profile each, so switching to an isolated profile moves you to that profile's window instead of paging inside the current one. macOS can mix profiles in one window because it keeps a Chrome `Browser` per profile per window. A Linux equivalent would need hidden companion Browsers per window.
  - The profile pager's swipe can briefly show an isolated profile's page in the wrong window before the routing switches windows. Its tabs show `ERR_PROFILE_WINDOW` and never load in the wrong profile.
  - A window restored from an older session with mixed-profile tabs keeps them, but only the window's own profile loads.
- Chrome's own "Open link in incognito window" (`OFF_THE_RECORD` disposition) still creates a Chrome OTR window that Ardia does not adopt. This branch doesn't change that.
- Engine events from OTR profiles are dropped, so downloads started in a private window don't appear in Ardia's Downloads view.
- The auto-PiP default content setting is still applied only to the app profile.
- `profiles.deleteData` and `profiles.clearBrowsingData` remain unavailable on Linux (`ERR_UNKNOWN_CALL`). `MarkDeleted` exists in the registry for when deletion is implemented.
