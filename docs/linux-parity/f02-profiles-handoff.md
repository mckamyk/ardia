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

Anything else is `ERR_PROFILE_INVALID`. Other errors: `ERR_PROFILE_DELETED`, `ERR_PROFILE_RELEASED` (a private name never comes back after release), `ERR_PROFILE_UNAVAILABLE`, `ERR_PROFILE_WINDOW` (a tab asks for a profile its window doesn't host) and `ERR_PROFILE_PRIVATE`.

There is no fallback anywhere. A tab, adoption or transfer works only when the resolved profile is the window's own page profile, and adoption across profiles is rejected.

Private windows that share one original profile share Chrome's primary OTR profile, as on macOS and in Chrome's own incognito. Chrome destroys that profile, discarding its data, when its last browser closes.

### Linux UI

- `apps/linux/src/hostWindowProfiles.ts`:
  - `createHostWindowOpener` replaces the fire-and-forget `ArdiaShell.openWindow`. If native creation fails, it emits `onWindowEvent {type: "close"}` for that window, so the shared store drops it and releases its private name. No phantom window remains. A private window always sends its own state as `privateWindow`. Without that state it fails rather than opening a regular window.
  - `withPrivateWindow` builds a private renderer's initial session from that seed (only that window and its own tabs). A private bootstrap without a seed fails at startup.
  - `installProfileWindows` keeps each regular native window on one engine profile:
    - Switching to a profile that shares the window's data switches in place.
    - Switching to an isolated profile focuses an existing window of that profile, or opens one.
    - Moving a tab to an isolated profile reopens its URL in that profile's window.
- `hostSession.ts`, `main.tsx` and `shims/shellBase.tsx` only register these. A private window doesn't adopt the default profile's parked pins.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest engine/ardiacore-linux/tests/test_profiles.py` | 2 passed. The compiled registry fixture covers parsing, lookup, no fallback, aliasing and OTR-as-regular rejection, private lifetime, disposal, release and delete during load, reentrancy and registry destruction. |
| `python3 -m unittest engine/ardiacore-linux/tests/test_session_merge.py` | 2 passed, including the new private-session rejections. They fail when the check is reverted. |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | 121 run, 116 passed. `test_launch_claims` fakes now model the window's hosted profile, including an isolated-profile window that refuses a default-profile launch offer. The 1 failure and 4 errors are the same as on `linux`: `test_chromium_close` and the `test_document_close_*` fixtures can't apply the 156-rebased close patch to their 155 fixture. They are unrelated to this branch. |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed |
| `pnpm --filter @ardia/linux test:host` | 207 passed, including 5 new tests in `hostProfiles.test.mjs`. The private-bootstrap case fails without the session hook. |
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
