# Linux native acceptance suite (#81)

One command runs the runtime acceptance scenarios for #11, #12, #13, #16, #17, #18, #19, #20, #22, #24, #25, #26, #27, #28, #30, #32, #33, #34, #35, #36, #37, #78, #102, #104, #105, #106, #107, #108 and the private-window follow-ups #71, #72, #73, #88, #89, #99 and #101 against a packaged Linux Ardia build. It runs headless, uses throwaway profiles, and prints the result for each issue as JSON. An issue can be closed with evidence once its result is `pass`. A `manual` result means every automated check passed and the steps that need a person are listed.

## Running it

```sh
pnpm --filter @ardia/linux build
pnpm --filter @ardia/linux acceptance -- <path to the packaged ardia executable> apps/linux/dist
```

- **UI directory.** The second argument is optional. When given, the build loads the UI from that directory (`--ardia-ui-dir`); otherwise it uses the bundled UI.
- **Paths.** Relative paths resolve against the directory you ran pnpm from.
- **Options:**
  - `--only F01,F03,#33` runs a subset, named by finding or by issue number.
  - `--results <dir>` chooses where evidence goes (by default a new `/tmp/ardia-acceptance-*` directory).
  - `--keep` keeps every scratch profile, not only failing ones.
  - `--list` prints the scenarios.
- **Exit status:** `0` when no issue failed, `1` when any issue failed, `2` for a usage error.
- **Timing.** The whole run takes about 15 to 25 minutes. Every wait has a deadline: a check stops after 60 s, a scenario after 3 to 8 minutes.

Results go to stdout and to `<results>/results.json`:

```json
{ "summary": { "pass": [13], "fail": [18], "manual": [11, 30, 32] }, "closable": [13],
  "results": [{ "issue": 13, "finding": "F03", "status": "pass", "automated": { "pass": 7, "fail": 0 },
                "manual": [], "checks": [{ "name": "site panel: mixed", "status": "pass", "detail": {"level": "mixed", "local": false}, "durationMs": 912 }],
                "evidence": "<results>/F03", "scratch": null }] }
```

- **Evidence.** Each scenario writes its JSON and screenshots under `<results>/<finding>/`.
- **Failures.** A failing scenario keeps its scratch profiles, including the browser log and `*-command.json` with the exact command line. Their paths are listed in `scratch`.

The harness itself is tested by `pnpm --filter @ardia/linux acceptance:self-test`. It needs no Ardia build:

- the CDP client, against a mock WebSocket peer;
- the launch plumbing and shell driver, against a mock CDP host that evaluates the driver's expressions in a fake `__ardiaHost` and store;
- the scenario runner (deadlines, critical checks, cleanup, manual roll-up) and the report;
- the classifiers, against recorded site-panel text and a recorded `navigator.locks.query()` result;
- the fixture servers and certificates;
- the parts that only a real Chromium can show, run against `/usr/bin/chromium` (or `ARDIA_ACCEPTANCE_CHROMIUM`) when one is installed:
  - SPKI trust and the bad certificate;
  - resolver mapping and blocking;
  - the mixed-form state;
  - gated downloads;
  - synthesized drops;
  - kiosk print to PDF;
  - process-group cleanup;
  - the content blocker's bridge page answering the native send and poll scripts from an isolated world, with an MV3 stand-in for uBlock Origin (stock Chromium no longer loads MV2).

## Environment of every launch

Each browser gets:

- **Its own profile.** A fresh `--user-data-dir`, also used as `ARDIA_DATA_DIR`. `HOME` and the XDG directories point inside it.
- **Headless display.** `--ozone-platform=headless` with SwiftShader, and `DISPLAY`/`WAYLAND_DISPLAY` cleared.
- **No session bus or printers.** It can't reach the session bus (so no portal dialog can appear on the desktop) or a CUPS server (so a print never reaches a real printer).
- **Debugging port.** `--remote-debugging-port=0`. The port is read from `DevToolsActivePort`.

Launches are background (`ARDIA_BACKGROUND=1`) except:

- F01's native-UI run, because background instances refuse print and DevTools by design;
- F22, which needs real activation.

Ozone headless still prevents any window from appearing or taking focus.

Each browser starts in its own process group. Cleanup closes it through CDP `Browser.close`, then sends `SIGTERM` and `SIGKILL` to that group only. Ctrl+C stops the test browsers before exiting.

Profiles are seeded with a one-window `linux-session.json` and a completed `onboarding.json`. The exceptions are F27's fresh and existing-profile cases, which seed what they test.

Local fixtures:

- **HTTP.** `*.ardia.test` and `*.ardia-keep.test` are resolved to 127.0.0.1 by `--host-resolver-rules`. F06 keeps its control site on `ardia-keep.test` because Chromium clears cookies per registrable domain. Every other host resolves to `NOTFOUND`, so nothing leaves the machine.
- **HTTPS.** Uses SNI:
  - `secure.ardia.test`, `mixed.ardia.test` and `suggestqueries.google.com` present a leaf signed by a CA generated for the run. Its key is trusted only through `--ignore-certificate-errors-spki-list`.
  - `badcert.ardia.test` presents an untrusted self-signed certificate.
- **Suggestions.** The default engine's endpoint (`suggestqueries.google.com`) is mapped to the local HTTPS server. It answers in the Firefox JSON shape and logs every request's headers.
- **Mixed content.**
  - Chromium autoupgrades mixed images, and loopback addresses are never mixed content. The mixed page therefore carries an insecure form action, which Ardia's security helper classifies as `mixed` (`contained_mixed_form`), and an `http://` image.
  - The stock-Chromium self-test confirms Chromium reports `contained-mixed-form` for that page.
- **Downloads.** Attachment endpoints, plus slow bodies. A slow body holds its last chunk until the scenario releases it, so pause, resume and cancel always act on a live transfer.
- **Content blocker.** F06 uses the uBlock Origin build packaged at `lib/ardia/extensions/ublock` (`scripts/linux/fetch-ubo.sh`, then `package.py --content-blocker`). Set `ARDIA_ACCEPTANCE_CONTENT_BLOCKER` to a prepared extension directory to pass `--ardia-content-blocker-dir` instead. The blocked request is `/ads/banners/<run>.png`, which only EasyList's bundled copy matches (`/ads/banners/*$image`); list updates cannot reach the network, so uBlock uses the lists in its package.
- **Page messaging and tint.** `/gestures` has a link, a cross-site iframe (`frame.ardia.test`) with its own link, a paragraph to select, a textarea with two matches and a tall block to scroll. `/tint/meta`, `/tint/header`, `/tint/background` and `/tint/plain` each carry one tint source (`#336699`, a sticky `#aa2244` header, a `#22aa66` body, nothing).
- **Page status.** `/fullscreen`, `/hover` (one large link), `/popup` (calls `window.open` while loading, without a gesture, so Chromium blocks it), `/busy` (spins forever 200 ms after load) and `/favicon/<label>` (links `/icon/<label>.png`). An icon whose name starts with `bad` answers `image/png` with a body that is not an image.
- **Print.** `--kiosk-printing` with the Save as PDF destination seeded in `Preferences` writes the PDF into the seeded download folder without a dialog. This was verified on stock Chromium 155.

`openssl` must be installed. Node 22 or newer is needed for the built-in WebSocket client; the suite was written on Node 26.

## What each scenario automates

| Finding | Issue | Automated checks | Manual |
| --- | --- | --- | --- |
| F01 | #11 | Two split panes with 3 and 5 matches; counts, next/previous wrap and Escape dismissal per pane; DevTools, Inspect and Console each attach only to the active pane (CDP `attached`) and Toggle closes it; Print writes a PDF; Save Page is accepted without a crash; a background instance refuses DevTools but still finds | System print dialog; Save Page output (always asks for a path); docked layout, splitter, overlays and focus return; page-focus Find and DevTools shortcuts |
| F02 | #12 | Isolated profile window has its own browser context, cookies, localStorage and history; host refuses a default-profile tab in an isolated window (`ERR_PROFILE_*`); shared-data profile shares cookies/storage and context; private window is a separate context, reads and leaks nothing, never reaches `linux-session.json`, and its data is gone after the last private window closes; a blocked profile directory leaves no phantom window; restart restores no private window and keeps regular data | Extension isolation; Settings panes per profile |
| F03 | #13 | Site panel text for HTTPS, HTTP, mixed, invalid certificate, HTTPS again, HTTP after HTTPS (no stale certificate) and `chrome://version`; web pages are never "Local Page" | Certificate viewer details |
| F06 | #16 | Notifications/location/camera set through `sites.setSetting` are what the page's `navigator.permissions` reports after reload and after restart; revoking one and resetting the site return them to `prompt` and drop the origin from `sites.origins`; private-profile and malformed requests are refused without changing the regular profile; `sites.clearData` removes one site's cookie and localStorage and keeps another's; a `profiles.clearBrowsingData` scoped by `since` removes the newer cookie and keeps the older one; Cancel in Clear Browsing Data keeps everything and Clear Data removes the last hour's history, cookies and storage; the content blocker reports ready with EasyList on; an EasyList-matched image never reaches the fixture server while a control image does; a site exception lets it through, covers subdomains, is announced by `contentBlocker.changed` and survives restart; Block Again, blocking off/on and EasyList off/on each change what reaches the server; the off directive and path-like list ids are refused | Site panel and Privacy settings showing the same state; native permission prompts with real devices |
| F23 | #33 | Four-page history; history menu jumps −2, +2, −3; zoom in matches `devicePixelRatio` and shows the `Zoom: 110%` indicator; the popover shows the percentage and its Reset returns to 100% and hides the indicator; zoom out and the reset command; per-site zoom listed by `sites.zoomLevels`, restored after restart with matching page scale, removed on reset | Pinch zoom |
| W92 | #92 | A window created in the save delay survives a reload of another renderer; a Little Ardia window survives a new window's startup sweep | — |
| W77 | #77 | A window seeded with a fullscreen stamp enters fullscreen at launch (CDP window state) while its windowed sibling stays windowed; after leaving fullscreen, a new window's renderer does not restore it again | Restore on a second monitor |
| W79 | #79 | A new window advertises held typeahead and reports readiness; Settings refuses it | Ctrl+N then immediate typing; typing at launch |
| W70 | #70 | A shared-data profile switches in place; an isolated profile opens its own window hosting that page profile and leaves the source window on its profile; switching again opens no second window | Trackpad swipes toward an isolated profile |
| W114 | #114 | From Settings, an isolated profile's zoom is set, listed only for that profile and reset; private names are refused for listing and writing | — |
| F07 | #17 | Status reports the store available, `backend: basic` and no reauthentication in a background instance; an empty vault lists `[]`; a credential saves through `ArdiaPasswords.save` and lists without its password; remove and never-save list work; a private profile name is refused with `ERR_PROFILE_PRIVATE` on generic and typed calls, and a credential saved in an isolated profile stays out of the default profile and the other way round; generic `passwords.reveal`/`passwords.save`/`autofill.cardNumber` are refused with `ERR_SECRET_ROUTE`; reveal, unlock, update and export are refused with `ERR_REAUTH_UNAVAILABLE`; addresses add, update and remove; a card saves through `ArdiaAutofill.saveCard`, lists only its last four digits and its number stays gated; password and autofill toggles change; the Passwords pane shows the basic-store notice; everything survives a restart; `Login Data`, `Web Data` and the browser log hold no fixture secret in plain text | polkit prompts (grant, deny, dismiss, timeout) for unlock, reveal, edit, export and card numbers; the five-minute grant window; a locked Secret Service; a private window's own renderer |
| F08 | #18 | Known file completes on disk and `fileStatus` sees it; Ctrl+J lists it; slow download progresses, pause stalls it, resume finishes it; cancel removes the partial file; remove forgets it in Chromium's history; the folder setting changes the destination; clear empties finished rows; restart shows the kept download once, nothing live, nothing cleared, no popover | Open, Show in Folder and Trash; ask-where-to-save dialog |
| F09 | #19 | With extensions enabled (`extensions: true`, the only scenario without `--disable-extensions`) and the fixture from `fixtures/extension` loaded by `--load-extension`: listing with action, popup, side panel and icon; unknown or malformed `extensions.*` calls refused; the omnibox keyword is in `search.engines` but not in Ardia's engines; off and on with `extensions.changed`; reload picks up an edited version; a badge set for one tab shows only there; toolbar activation opens a real popup that closes with its host on tab switch; `openPanelOnActionClick` opens the side panel; Load unpacked inspects and installs; uninstall removes the extension, its side panel and its keyword | Web Store consent and Cancel; permission-increase re-enable prompt; folder picker; popup placement, focus and keyboard |
| F20 | #30 | Sidebar drop area found by its DOM `drop` listener; dropping one URL, several URLs and address-like text opens tabs; prose opens nothing; a CDP file drop (DOM-only paths) opens no `file:` tab; malformed `javascript:`/comment/NUL lists open nothing unsafe | Drag out of Downloads; real file-manager drops; internal reorder and page drops |
| F10 | #20 | With suggestions on, typed queries get the local endpoint's suggestions and requests carry no cookie (a `SID` cookie is planted for the host) or authorization; with suggestions off, zero requests and no remote rows; a private window gets suggestions without cookies | — |
| F12 | #22 | Generated scratch Chrome and Firefox profiles under the launch `HOME` are listed with their importable kinds; a Chrome import returns 4 bookmarks in nested folders, 3 history entries, a 3-password token and a keyring note, with no plaintext password in the renderer reply; `writePasswords` finishes by event with `added: 3, existing: 0` and Ardia's own `Login Data` holds exactly those 3 passwords (decrypted with the basic key and compared); importing them again reports `added: 0, existing: 3`; a reused or unknown token is `expired` without resolving a profile; writing to the isolated `work` profile stores the 3 passwords in `Profile work/Login Data` and leaves the default profile's 3; private and invalid targets get `ERR_PROFILE_PRIVATE`/`ERR_PROFILE_INVALID` and lose the token; a corrupt History fails only history; cancelling a 40,000-row history import reports `cancelled` and leaves no `ardia-import-*` copy in `TMPDIR`; Firefox imports bookmarks and history and reports passwords as unsupported; HTML and CSV files are refused until the chooser picks them, and a headless chooser cancels or reports a disabled policy; every source file hashes the same afterwards | Import window click-through; portal/GTK chooser; Settings CSV import; Chromium's SQLite build on real profiles; keyring-protected (v11/v12) Chrome passwords |
| F13 | #23 | Two scratch profiles share one temporary folder (`--ardia-sync-folder`); the `ArdiaSync.*` capabilities are present and Sync is off rather than unavailable; with two browser windows exactly one renderer is the sync authority and the other gets `ERR_SYNC_NOT_AUTHORITY` for folder writes while mirroring its state; Turn On Sync on A writes only `NNS1` files padded to 1 KiB with no title, URL, run id, host name or phrase in any name or content; B refuses a stranger's phrase (`mismatch`) and 23 words (`wordCount`), then joins with the kit's numbered, upper-case lines (the phrase is read from A's test-only key file); bookmarks go both ways; a restarted B resumes from its stored key and receives A's next bookmark; Stop Syncing on B keeps its bookmarks and deletes its key | The Secret Service keyring item; no or locked keyring; polkit-gated words, QR, Copy Words and Save as Text; the folder picker and Show Sync Folder; Linux and macOS through a real synced folder |
| F14 | #24 | Against a private `dbus-daemon` running the fake Evolution Data Server (`engine/ardiacore-linux/tests/fixtures/fake_eds.py`): before connecting, `ArdiaCalendar.status` is `notDetermined` with EDS available, `sources` is `ERR_CALENDAR_NOT_CONNECTED` and EDS saw no source or calendar call; connecting lists the two calendars with color, account and type and writes `linux-calendar.json`; today's events include a daily New York series at the right instant, a local all-day event, a declined and a cancelled meeting and the personal calendar's event; the Join alert picks the accepted meeting; unchecking a calendar stops its reads; an EDS change appears without a refresh; stopping EDS turns the calendar `restricted` and clears it. A second launch with no session bus and a saved consent is `restricted`, and Settings › Calendar shows the Evolution Data Server wording and Try Again without macOS text | A real GNOME Online Accounts calendar; an expired online account signing in again |
| F22 | #32 | With sway or weston installed: the first window is active, Ctrl+N moves `document.hasFocus()` to the new window, a background window takes no focus, Settings comes to the front | Without a compositor, all of it; with one, immediate typing, restore-behind and links from other apps |
| F26 | #36 | Settings opens; no Engines pane; no Default browser, Open Ardia at login, Scroll bars or `ardia` command rows in General, Appearance or Advanced; no `ArdiaSystem`/`ArdiaApp`/unhandled rejections in the shells' console (replayed on attach) or the browser log | Default-browser and autostart across restart; streaming identity; header dragging; scroll bar style |
| F17 | #27 | Page fullscreen from a gesture sets the page's `fullscreen` and leaves the window's `fullScreen` unset, and exiting clears it; hovering the fixture link (CDP mouse events into the page) sets `status` to its URL, moving away and then navigating clears it; the load-time `window.open` is listed in `popups`, the prompt's Only Once opens it as a tab; `Page.crash` shows "This tab needs to reload" and Reload clears `crashed` (this launch runs with core dumps off, so systemd-coredump can't hold the crashed renderer); a spinning page with an unacknowledged key event, sent once the page has started spinning and before the harness detaches its CDP session because Chromium ignores hangs while a debugger is attached, shows "Page Unresponsive" within 55 s, Wait closes it, the hang is reported again and Exit Page ends the renderer (`crashed`) | Video fullscreen from a real player; physical hover; split and transfer |
| W78 | #78 | Page fullscreen leaves the window's `fullScreen` unset; leaving it keeps the window normal; inside a fullscreen window (CDP `Browser.setWindowBounds`) entering and leaving page fullscreen keeps the window fullscreen | Compositor fullscreen key |
| F24 | #34 | A fresh profile requests `/icon/<label>.png` and the shell shows a PNG data URL; `favicons.fetch` returns a PNG of at most 64 px through the page profile and the request carries no cookie; a malformed image, an unreachable host and a `file:` URL return null; a loopback literal, `localhost` and a name resolving to a loopback port that is not marked public (the launch passes `--ip-address-space-overrides` for the HTTP fixture port only) return null and never reach the fixture; the icon is still shown after restart | Essentials and top-strip icons |
| F27 | #37 | Fresh profile: onboarding appears once, `onboarding.json` gets `startedAt`, the native launch record (`ArdiaShell.launchSteps`) lists all four steps and the startup renderer carries the `ardia.linux.app-launch` session mark; an unfinished onboarding resumes after restart without the intro; a completed one never returns; an existing two-window profile never onboards, the launch record is complete and exactly one renderer carries the mark, the profile is marked complete, and a reload of the owner does not onboard; Release Notes answers in its own window | Finishing onboarding through its steps; setup choices across windows; onboarding from a utility window |
| F15 | #25 | A local media page (looping WAV tone, Media Session metadata and handlers, autoplay allowed by `--autoplay-policy`) sets `s.live[tab].playingAudio`; title, artist, album, actions and `controllable` reach `useMedia().sessions`; pause, toggle, next and seekTo from Ardia reach the page's handlers and the shared playback state; an unknown command is refused; mute is accepted; the native PiP probe answers `false` and an audio-only page opens no PiP window; without user activation `requestPictureInPicture` and `takeControlPress` return `false`; a playing background tab refuses `sleepTab`, `discard` and `setFrozen` and keeps playing; navigating away clears the player and the badge; with the fixture origin's autoplay set to block, a play without user activation stays paused and one with activation plays | Manual PiP of a real video; manual PiP of a video outside the media session; automatic PiP on Hyprland; hearing mute; portal capture indicators |
| F18 | #28 | `engine.systemState` reports memory pressure, physical memory, `memorySignal` (`psi` when the kernel has `/proc/pressure/memory`, otherwise `meminfo`) and `power` (`upower` or `unavailable`, with no battery when unavailable); the visible tab refuses discard and freeze and `discard({unload})` is `unsupported`; an idle background tab reports clean input, sleeps through the shared `sleepTab`, answers `already` to a second discard and keeps its Ardia tab; activating it wakes it on the same URL with back history; typed form input reports `unsavedInput` and protects the tab from sleep, discard and freeze; an audible background tab refuses discard; keys typed into a page reach the shell as `engine.inputActivity`; with a second window open, `navigator.locks.query()` shows the `ardia.linux.tab-policies` lock held once and waited on once | Battery transitions; power-saver profile; real memory pressure |
| S102 | #102 | With `--password-store=gnome-libsecret` and no session bus, both status calls report `protection: obfuscated`, `protectionFallback: true` and a storage label naming the built-in key, while the stores stay available; with `--password-store=basic` the protection is `obfuscated` without `protectionFallback` | A working Secret Service or KWallet reported as `keyring`; a locked keyring flagged in Settings |
| S104 | #104 | Settings › Passwords and › Autofill contain no "Mac", "macOS", "Keychain", "Touch ID" or "Applications folder"; Passwords names the system keyring, the system password and the "Storage on this computer" row | Layout on a desktop |
| S105 | #105 | The package's `install.sh` puts the policy at `DESTDIR/usr/share/polkit-1/actions` (mode 0644) for `/usr` and `/opt/ardia` and writes an absolute launcher; when the policy is installed system-wide, `pkaction` lists the action with `auth_self` for active sessions; a non-background instance's status reports the missing policy with its directory (or no `policyMissing` once installed) | A root install with `pkaction` and the F07 polkit prompt checks |
| S106 | #106 | A fresh profile restored onto the blocker fixture page loads it with the EasyList-matched image never requested, and the log has no "held navigations go ahead" warning | — |
| S107 | #107 | From uBlock Origin's background page, `tabs.executeScript` into a `chrome://version` tab is refused; the `chrome://ardia/` shell has no execution context from the component | — |
| S108 | #108 | An isolated profile's window blocks the fixture ad; a site exception set from the default window lets it through there while other sites stay blocked; a call from the isolated window removes the exception from the default profile's settings and blocking returns | The site panel in an isolated window |

S106, S107 and S108 need `ARDIA_ACCEPTANCE_CONTENT_BLOCKER` (or a package built with `--content-blocker`). S105's installer check needs the suite to run against an extracted package, where `install.sh` sits beside `bin/` and `lib/`.
| F02-88 | #88 | A private window's `sites.setSetting` and `sites.setZoom` show in its own `sites.settings`/`sites.zoomLevels` and never in the regular profile's (nor in `sites.origins`); `history.query` and `profiles.clearBrowsingData` from it are `ERR_PROFILE_PRIVATE`; a regular window naming the private session gets `ERR_PROFILE_WINDOW`; a new private session after the last window closed has none of it | — (a call during OTR shutdown is covered by the fixtures) |
| F02-72 | #72 | A private download asks where to save, as Chrome's incognito does (the background instance answers from `file-chooser.txt`, see #194), reaches only the private renderer, flagged `offTheRecord`, and its `downloads.list`; regular renderers never receive it and cannot remove or pause it; a slow private download pauses, resumes and cancels from its window; after the window closes and after a restart nothing lists it | The private window's Downloads popover |
| F02-71 | #71 | (the browser starts) | Open Link in Incognito Window from a regular and an isolated window opens an Ardia private window, never a plain Chrome window |
| F02-89 | #89 | A second regular window's `ArdiaShell.privateWindows` lists the private window as a bare id while `windowIds` keeps hiding it; `focusWindow` from that window succeeds; close, frame, title and minimise from it are refused and the window stays; the entry goes once it closes | The Window menu entry (F02-148) |
| F02-101 | #101 | Find counts matches in an isolated profile's window and in a private window (background instance); DevTools opens a frontend for a private window's page (native instance) | Print preview and Save Page in isolated and private windows |
| F02-73 | #73 | Turning automatic PiP off writes `BLOCK` (2) into the default profile's and a later-loaded isolated profile's `Preferences`; turning it on writes `ALLOW` (1) into both | — |
| F02-99 | #99 | With a split-mode fixture: without incognito access the private window gets no action state and `extensionView.create` is `ERR_EXTENSION_PRIVATE`; after `extensions.configure` grants it, the private renderer learns it, gets action state and opens the popup; `extensions.setEnabled` from the private window is `ERR_PROFILE_PRIVATE` | Toolbar, popup and side panel by hand; spanning-mode extensions are F02-149 |
| F02-148 | #148 | A second regular window's `ardiaLinux.windowMenu.open` shows Minimize, Maximize, Downloads and Merge All Windows, both regular windows by title (its own ticked) and "Private Window" with no trace of the private tab; choosing that entry resolves the menu and closes it; the private window's own menu ticks itself once | Right-clicking the window controls and the focus change |
| F02-149 | #149 | With a spanning-mode fixture granted incognito access and pinned, the private renderer gets its action state on its active tab and opens its popup; a badge set from the service worker on the private tab reaches that renderer's action state within 8 s and never the regular window's tab | The button and popup by hand |
| F02-150 | #150 | (the browser starts) | Open Link in Incognito Window from an extension popup and from DevTools opens an Ardia private window |
| F02-172 | #172 | A finished private download removed from its window raises `downloads.removed` in that renderer only; closing the private session sends the regular renderer no removals | — |
| F02-173 | #173 | With automatic PiP on, the regular profile's default is `ALLOW` while a private window is open | A registered video in a private window doesn't auto-enter PiP; the same page in a regular window does |
| W74 | #74 | `ArdiaShell.pickFolder` is advertised; a background instance returns no folder instead of a dialog; malformed requests get `ERR_FOLDER_REQUEST` | The real folder dialog: pick, cancel, unwritable folder |
| W75 | #75 | With `PATH` emptied and no session bus, Show in Folder fails with `ERR_REVEAL_UNAVAILABLE`; when `/dev/shm` is a separate mount, a download there moves to `/dev/shm/.Trash-<uid>` | A finished row never flashing as missing; Show in Folder through a portal or FileManager1 |
| W156 | #156 | With no session bus and a folder handler whose `xdg-open` exits 4, Show in Folder fails with `ERR_REVEAL_FAILED` naming the folder app, after running `xdg-open .` in the download's folder; when it exits 0, Show in Folder succeeds | A portal or FileManager1 call that fails; trash on a removable drive whose top folder others can write |
| W76 | #76 | DevTools asked for as soon as a new tab's page handle exists, before its first viewport, opens a frontend and inspects that page (screenshot evidence) | Placement and focus on a fresh tab |
| W137 | #137 | The extension service answers in the window | Closing the Web Store tab dismisses its install prompt; a CRX prompt stays until answered |
| F16 | #26 | The page's main world sees no `post`, `__ardiaHost` or `webkit`; a synthetic shift-click and forged `peek`/`linkDrop` `postMessage`s open nothing; a CDP shift-click on a link opens a peek on it and Alt+Shift opens a split pane; a shift-click on a link in a cross-site iframe opens a peek; a double-click selection reaches `ardiaLinux.selection` and Escape in the page clears it; jump to selection scrolls back to it; Copy Quote Link shows its toast; replace in a focused textarea rewrites both matches | Physical modifier clicks and changed modifiers; the popover's position and Escape; dragging a link out of a page |
| F25 | #35 | A sticky colored header, a body background and a theme-color meta each set `themeColorSource` and the tab's `themeColor` to their color, and a plain page resets it to white; wheel scrolling a long page down sets the toolbar's `scrolledDown` and scrolling back to the top clears it | The toolbar slide and the setting that turns it off; the rendered tint and Extend Website Color |
| I109 | #109 | A private `dbus-daemon` runs the generated fake Secret Service (`engine/ardiacore-linux/tests/fixtures/fake_keyring.py`) holding a locked "Chrome Safe Storage" item. Discovery marks Chrome `needsKeychain` without unlocking; without consent the `v11` passwords stay locked; a dismissed prompt reports `denied` and imports nothing; an allowed prompt unlocks the key, both passwords import into Ardia's `Login Data`, and `forgetUnlockedKeys` drops the key | GNOME Keyring and KWallet with their real prompts |
| I110 | #110 | NSS (`certutil` + `PK11SDR_Encrypt`) generates `key4.db`/`logins.json` with and without a primary password. The plain profile imports and saves 2 passwords; the primary-password profile reports `locked`, stays locked with a wrong password, and imports 2 with the right one; no password reaches the renderer | A real Firefox profile |
| I111 | #111 | A generated Chrome profile (SNSS session, v24 `Cookies` with host-hashed v10 values, `Web Data` address and card) and a Firefox profile (mozLz4 session store, `cookies.sqlite` with a container cookie). Discovery advertises the kinds; the Chrome import returns 2 tabs and a vault token with no cookie value or card number in the reply; `writeImported` stores 2 cookies, 1 address and 1 card in Ardia's profile (decrypted and compared), and the token is single use; a private target loses it; Firefox imports 1 tab and 1 cookie and skips the container; sources hash the same and no scratch copy is left | Import window click-through with a real profile |
| E97 | #97 | The follow-up fixture (`fixtures/extension-followups`, loaded by `--load-extension`) declares `chrome_settings_overrides.search_provider`; `search.engines` lists it with `isOmniboxExtension: false` and keyword `ardiasearch`; Ardia maps it into `settings.extensionSearchEngines`; uninstalling removes it from both | The provider row in Settings › Search |
| E98 | #98 | Chromium assigns the fixture's `_execute_action` (`Alt+Shift+Y`) and named (`Alt+Shift+U`) shortcuts on Linux (`chrome.commands.getAll()`) | Pressing the shortcuts: popup under Ardia's toolbar button, `onCommand`, no shadowing of an Ardia shortcut, side panel through the action |
| E100 | #100 | A generated echo host (Node, `stdio`) answers `chrome.runtime.sendNativeMessage` from `~/.config/chromium/NativeMessagingHosts` (Ardia's new lookup) and from `~/.config/google-chrome/NativeMessagingHosts` (Helium's); Ardia's own `<data dir>/NativeMessagingHosts` wins over both; a host that doesn't list the extension is forbidden and an unknown host isn't found | A real password manager's desktop app connecting |

## Manual steps

The JSON lists each manual item with these steps. They are kept here so they can be run without the suite.

**#11 F01**

- **System print.** Open two split panes with different text. Focus pane B and press Ctrl+Shift+P. The GTK or portal dialog previews pane B, and printing to a file writes pane B's text.
- **Save Page.** Focus pane B and press Ctrl+S. Save as "Webpage, Complete" and as "Single File" in a scratch folder. Both files contain pane B's text, and the complete variant has its `_files` folder.
- **Docked DevTools.**
  1. Open DevTools on pane A. Dock it bottom, right and left, and drag the splitter each time.
  2. Overlays (find bar, menus) draw over the frontend.
  3. Close DevTools: focus returns to pane A.
  4. Undock it: its own window opens, and Toggle reveals it.
- **Page-focus shortcuts.** Click into pane B's page and press Ctrl+F: pane B's find bar opens and pane A's stays closed. Press Ctrl+Alt+I, Ctrl+Shift+J and Ctrl+Shift+C. DevTools opens on pane B each time. CDP key events sent to a page never reach Ardia's window-level shortcut handler, so these stay manual.

**#12 F02**

- **Extensions.** An unpacked extension installed in the default profile is absent from the isolated profile, and the other way round.
- **Settings.** Settings opened from the isolated window shows only that profile's History, Passwords and Site Settings.

**#13 F03**

- **Certificate viewer.** Its issuer, validity and fingerprint match the served certificate.

**#16 F06**

- **Site panel and Settings.** Open a site's Site Controls panel: Camera, Location and Notifications show the values set in Settings › Privacy › Site settings. Change one in the panel: Settings shows it after reopening, and the page's `navigator.permissions` agrees after reload. The panel's content blocker toggle turns blocking off for that site, and Settings › Privacy lists it under Sites without blocking.
- **Native prompts.** With a real camera and microphone, allow a page's request with Remember: the choice appears in Site settings and survives restart; Block likewise.

**#17 F07**

- **System authentication.** Install `share/polkit-1/actions/dev.ardia.linux.policy` into `/usr/share/polkit-1/actions` and run a polkit agent. Unlock asks for your password and lists the passwords. Opening one within five minutes shows it without a second prompt; after five minutes it prompts again. Export CSV… shows the save dialog, then always prompts, and leaves the file at mode 0600 (Chromium writes it under the umask, then changes the mode). Showing a card number always prompts.
- **Refusals.** Deny the prompt, dismiss it, and leave it open for two minutes. Each time the password stays hidden, Edit doesn't save, Export writes nothing, and the card number stays masked.
- **Locked keyring.** With `--password-store=gnome-libsecret` and a locked or absent Secret Service, the Passwords pane shows "Passwords are unavailable" with the keyring reason, not an empty vault.
- **Private windows.** A private window's own renderer gets `ERR_PROFILE_PRIVATE` for every passwords and autofill call and never shows the regular profile's data.

**#18 F08**

- **Open, Show in Folder and Trash.** In a desktop session:
  - Clicking a finished row opens the file.
  - Show in Folder selects it in the file manager. Check each path: portal, FileManager1 and `xdg-open`.
  - Move to Trash puts it in `~/.local/share/Trash/files` with a `.trashinfo` record.
- **Ask where to save.** With "Ask where to save" on, downloading shows the native dialog. Cancelling leaves no live row.

**#19 F09**

- **Web Store.** Choose Add to Ardia on a store extension. Ardia's dialog lists its permissions. Cancel installs nothing; Add installs it and shows it on the toolbar.
- **Remove from the Web Store (#169).** On an installed extension's store page the buttons read Add to Ardia and Remove from Ardia. Remove from Ardia opens Ardia's own removal confirmation, not Chromium's dialog; confirming removes it and the store page switches back to Add to Ardia; cancelling keeps it and the store page's call reports it was kept. Leaving the page while the confirmation is open keeps the extension.
- **Popup size (#169).** A fixture popup that grows after load (adds rows on a timer) resizes the popup to its new size up to 800 × 600 with no page script; a popup page's console shows no Ardia evaluation.
- **Permission increase.** Install a packed CRX, then a newer CRX of the same key that adds a permission. Chromium disables it; turning it on asks first, and Cancel keeps it off.
- **Load unpacked.** Settings › Extensions › Load Unpacked opens the native folder picker, and the chosen fixture folder installs after Ardia's dialog.
- **Popup.** Pin the fixture and click it: the popup opens under the button at its own size, takes typing, and closes on Escape, on an outside click and from its own Close button.

**#24 F14**

- **Real account.** Add a Google or Nextcloud account in GNOME Online Accounts with Calendar on. Settings › Calendar › Connect Calendar… lists its calendars with their colors; a pinned calendar.google.com shows today's events on hover, the countdown badge and, a minute before a meeting with a Meet link, the alert with Join.
- **Expired account.** Revoke the account's access at the provider and refresh: the preview keeps what it has, GNOME Online Accounts asks to sign in again, and new events appear afterwards without restarting Ardia.

**#23 F13**

- **Keyring.** Run Ardia normally in a GNOME Keyring or KWallet session. Turn On Sync stores an "Ardia Sync Key" item (account = the device id) and nothing under the profile folder; a restart resumes without the phrase.
- **No or locked keyring.** With no Secret Service, or a locked keyring and a dismissed unlock prompt, Turn On Sync says the keyring isn't running or is locked and writes nothing to the folder. With sync on, a locked keyring at launch shows "not syncing" with the keyring message, and entering the phrase resumes.
- **Recovery phrase.** With the F07 polkit policy and an agent: Show Words, Show Code and Copy Words prompt once, then work for 60 s; Stop Syncing ends that early. The Recovery Kit sheet offers Close and Save as Text… only (no preview, PDF, Copy or Share); Save as Text… shows the save dialog, then prompts, and leaves a 0600 file with the 24 words. Denied or dismissed prompts show, copy and write nothing.
- **Folder.** Choose… opens the picker over Settings, and the browser window's sync uses the chosen folder. Advanced › Open Sync Folder opens it in the file manager.
- **Wording (#147).** The Sync pane, its sheets and the Sync search result say computers or devices, never Mac, iCloud, Finder, iPhone or Keychain.
- **Passwords (#145).** With sync on and Passwords ticked, a login saved on one Linux profile appears in the other device's Passwords pane after a sync, and a changed password replaces it. No polkit prompt appears for syncing. With the keyring locked, or `--enable-features=SkipUndecryptablePasswords`, passwords are skipped and nothing is removed on either side. The browser log, the sync folder and `sync-state.nns` never contain the password.
- **Large state (#146).** A profile whose sealed `sync-state.nns` grows past 8 MiB (a long history) keeps saving it: no "couldn't save the sync state" in the log, the file grows past 8 MiB, the journal parts are pruned after the save, and a restart loads it.
- **Linux and macOS.** Through Syncthing, Dropbox or a NAS share with a Mac on the current release, each side joins with the other's phrase, and bookmarks, history, settings, pinned tabs and passwords go both ways. A file still arriving shows "waiting for N files" and applies once whole.

**#30 F20**

- **Drag out.** Drag a finished download into a file-manager folder, on Wayland and on X11. The file copies, the press alone does not open it, and releasing before the drag starts does nothing.
- **Drop in.** Drag one, then three files from the file manager onto the sidebar and onto the top strip. Each opens as a `file://` tab, and the drop indicator shows and clears.
- **Existing drags.** Sidebar reorder, tab-to-page split and link-to-text-field drops still work.

**#22 F12**

- **Import window.** With a Chrome and a Firefox profile in `HOME`, open File › Import from Another Browser. Chrome is selected with Bookmarks, History and Passwords, and Continue imports them. The Done step shows the source's counts. Settings › Passwords lists the passwords after unlocking. Firefox offers Bookmarks and History only.
- **File chooser.** "Bookmarks HTML File…" opens the portal or GTK chooser over the Import window. Cancelling imports nothing. Choosing an exported `bookmarks.html` adds its folders under "Imported from HTML File" with the right link count.
- **Settings CSV import.** Settings › Passwords › Import a CSV with three new logins and one duplicate row. The pane says "Imported 3 passwords" and lists them. Importing the same file again says "Imported 0 passwords".
- **Chromium's SQLite build.** Import from current Chrome and Firefox profiles copied into a scratch `HOME`. History and bookmarks match the source's counts. The fixtures use the system SQLite. Chromium's build omits UPSERT, window functions and JSON, so only a real build proves the readers' queries don't need them.
- **Keyring-protected passwords.** Import from a Chrome profile that uses the GNOME keyring or KWallet. The Done step notes the passwords that weren't imported, and nothing claims success for them.

**#32 F22**

- **New window.** On Hyprland and on X11, press Ctrl+N and type `abc` at once. The new window is in front and holds `abc`.
- **Background windows.**
  - A restored back window stays behind on relaunch.
  - A link from another app doesn't steal focus unless it needs a new foreground window.
  - `ARDIA_BACKGROUND=1` instances never activate.
- **Utility windows.** Settings and Import open in front with keyboard focus. Reopening Settings focuses it.

**#36 F26**

- **Default browser and autostart.** When the backends land: `xdg-settings get default-web-browser` shows `ardia.desktop`, and `~/.config/autostart/ardia.desktop` exists. Both still hold after a session restart.
- **Streaming.** A Widevine test page plays and reports Chromium brands.
- **Header dragging.** Dragging the sidebar header moves the window on Hyprland and on X11, and double-clicking it maximizes.
- **Scroll bars.** Once supported, the System/Thin choice applies to a real page after restart.

**#27 F17**

- **Video fullscreen.** Play a video on a real site and use its fullscreen button. Ardia's sidebar and toolbar hide; Escape restores them in the same window state.
- **Hover.** Move the mouse over a link and off it. The status text shows the link and clears.
- **Split and transfer.** Open a split, hover a link and trigger a pop-up in each pane. Move one pane's tab to another window; its status and pop-up list go with it and the old window shows none.

**#34 F24**

- **Pinned layouts.** Pin the fixture page as an essential and switch to the top tab strip. The fetched icon shows in the tile and the strip, not the generic globe.

**#78**

- **Compositor fullscreen.** Make an Ardia window fullscreen with the window manager's key, then enter and leave fullscreen on a page. The window stays fullscreen with Ardia's chrome shown again.

**#37 F27**

- **Onboarding.** Finish onboarding through every step. It doesn't return after a restart, and Help › Show Onboarding replays it.
- **Setup choices.** Choices made in onboarding (search engine, top tab layout, two pins) appear in a second window and survive a restart without duplicate pins.
- **Utility windows.** Show Onboarding and Tool Tour run from Settings play in a browser window.

**#25 F15**

- **Manual PiP of a real video.** Play a video page (for example a local WebM) and use the tab's Picture-in-Picture control in the sidebar or mini player. A PiP window opens and the control shows it open; pressing it again closes the window and returns to the tab.
- **Manual PiP of a video outside the media session (#124).** Open a page with a muted, looping video and no audio, so it never joins the Media Session, plus a smaller second video. The tab's Picture-in-Picture control opens the larger playing video; a video with `disablepictureinpicture` is skipped; a `window.open` from a timer afterwards is still blocked, so the page kept no user activation.
- **Automatic PiP.** With Settings › Tabs › Automatic Picture in Picture on, switch away from a playing video tab: the PiP window opens at the configured Hyprland placement and closes when you return. With the setting off nothing opens.
- **Mute.** Mute a playing tab from its audio badge: the sound stops and the badge shows muted; unmuting restores sound.
- **Capture indicators.** Share a screen through the portal picker on a test page: the tab shows the screen-sharing indicator while capturing, and it clears when sharing stops.

**#28 F18**

- **Battery.** On a laptop with UPower, set Battery Saver to When unplugged. Unplugging reports `onBattery` at once and shows one Battery Saver toast, in the focused window only, even with several windows open; plugging in turns it off.
- **Power profile.** Switching power-profiles-daemon to power-saver turns `lowPowerMode` on at once and Battery Saver activates.
- **Memory pressure.** Run a memory hog until tasks stall on reclaim (`stress-ng --vm 4 --vm-bytes 95%`, or a cgroup with a low `memory.high`). Within 2 s `memoryPressure` reports `warning` (`critical` once full stalls pass 10%), idle background tabs sleep on the next sweep, and it returns to `normal` a few seconds after the hog stops.

**#71, #72, #89, #99, #101, #148, #149, #150 and #173 (private-window follow-ups)**

- **#71 Open link in incognito.** In a regular window, right-click a link and choose Open Link in Incognito Window. An Ardia private window opens on that URL and no plain Chrome window appears. From an isolated profile's window, the private window belongs to that profile.
- **#72 Downloads popover.** Download a file in a private window: Ctrl+J there lists it, Ctrl+J in a regular window doesn't, and closing every private window leaves the regular list without it.
- **#89 and #148 Window menu.** Open a second regular window and a private window. Right-click the window controls of the second window: the Window menu lists both regular windows by title and "Private Window", and choosing it brings that window forward.
- **#99 and #149 Extensions.** Allow a split-mode extension in incognito: its button shows in a private window and its popup and side panel open there. Do the same with a spanning-mode extension: its button shows too and its popup runs in the regular profile.
- **#150 Incognito links from surfaces.** From an extension popup and from DevTools, Open Link in Incognito Window opens an Ardia private window of that window's profile, never a plain Chrome window.
- **#173 Automatic PiP.** With automatic PiP on, a registered video in a private window doesn't enter PiP on a tab switch; in a regular window it does.
- **#101 Print and Save Page.** In an isolated profile's window and in a private window, Ctrl+P opens the print preview for that page and Ctrl+S opens the save dialog and writes the page.

**#26 F16**

- **Physical modifier clicks.** Shift-click a link with a real mouse: a peek opens at the click. Alt+Shift-click: it opens in a split pane. Change the peek and split modifiers in Settings › Tabs and repeat; the new keys apply without reloading.
- **Selection popover.** Drag-select a sentence: the popover appears beside it. Escape closes it.
- **Drag a link out.** Drag a link from a page and drop it back over the page with no drop target: one foreground tab opens on the link. Dropping outside the window or on a drop target opens nothing.

**#35 F25**

- **Toolbar.** With Hide Toolbar While Scrolling on, scroll a long page down: the toolbar slides away; scroll up: it returns. With the setting off it stays.
- **Tint.** A site with a colored header and no theme-color, then one with a theme-color: the sidebar and toolbar take each color and reset on a plain page. Extend Website Color off keeps Ardia's colors.

**#109, #110, #111 import follow-ups**

- **Keyring.** Import passwords from a Chrome profile that uses gnome-libsecret, then from one that uses KWallet. The import window asks to unlock, and the system prompt appears. Allow imports the passwords; Cancel imports none and shows the locked note.
- **Firefox.** Import passwords from a real Firefox profile. The count matches about:logins. A profile with a primary password reports locked (#178 adds the prompt).
- **Tabs, cookies and autofill.** Import them from a real Chrome profile. The tabs open in an Imported group, a site is still signed in, and Settings › Autofill lists the address and card.

**#97, #98, #100 extension follow-ups**

- **Search provider.** Load `fixtures/extension-followups` unpacked. Settings › Search lists Ardia Fixture Search; removing the extension removes it.
- **Shortcuts.** Pin the fixture. With a page focused, `Alt+Shift+Y` opens its popup under Ardia's toolbar button, and holding the keys doesn't reopen it. `Alt+Shift+U` records `fixture-command` in the service worker's `globalThis.commandReports`, with the page focused and with the sidebar focused. Assign `Alt+Shift+U` to an Ardia command in Settings › Keyboard Shortcuts: Ardia's command runs and the fixture records nothing.
- **Password managers.** Install 1Password, Bitwarden or KeePassXC with browser integration for Chrome or Chromium, then its Web Store extension in Ardia. The extension connects to the desktop app.

## Notes for the first real run

The scenarios drive the shared UI through the same seams the existing keyboard test uses:

- `ardiaLinux.store`, `ardiaLinux.pages` (the page store: fullscreen, status, crash, hang and pop-ups), `ardiaLinux.emitCommand`;
- `ardiaLinux.webviews` (the shared page handles), `ardiaLinux.media` and `ardiaLinux.lifecycle` (`state`, `sleepTab`) for F15 and F18;
- `ardiaLinux.toolbar` (the toolbar auto-hide store) and `ardiaLinux.selection` (`state`, `jump`, `copyQuoteLink`, `replaceInField`) for F16 and F25;
- `__ardiaHost.invoke` and `__ardiaHost.call`;
- `[data-ardia-linux-window]`.

They also use visible labels from the shared components: `Site Controls`, `Go Back`/`Go Forward`, `Zoom: N%`, `Find on Page`, `Reset`, `Skip` and the Settings pane names.

If a scenario fails because a label or flow changed rather than because the feature is broken, the failing check's `detail` says what it waited for. The kept scratch profile has the browser log for the first real triage.
