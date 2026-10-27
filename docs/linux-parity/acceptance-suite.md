# Linux native acceptance suite (#81)

One command runs the runtime acceptance scenarios for #11, #12, #13, #17, #18, #20, #30, #32, #33, #36 and #37 against a packaged Linux Ardia build. It runs headless, uses throwaway profiles, and prints the result for each issue as JSON. An issue can be closed with evidence once its result is `pass`. A `manual` result means every automated check passed and the steps that need a person are listed.

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
  - process-group cleanup.

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

- **HTTP.** `*.ardia.test` is resolved to 127.0.0.1 by `--host-resolver-rules`. Every other host resolves to `NOTFOUND`, so nothing leaves the machine.
- **HTTPS.** Uses SNI:
  - `secure.ardia.test`, `mixed.ardia.test` and `suggestqueries.google.com` present a leaf signed by a CA generated for the run. Its key is trusted only through `--ignore-certificate-errors-spki-list`.
  - `badcert.ardia.test` presents an untrusted self-signed certificate.
- **Suggestions.** The default engine's endpoint (`suggestqueries.google.com`) is mapped to the local HTTPS server. It answers in the Firefox JSON shape and logs every request's headers.
- **Mixed content.**
  - Chromium autoupgrades mixed images, and loopback addresses are never mixed content. The mixed page therefore carries an insecure form action, which Ardia's security helper classifies as `mixed` (`contained_mixed_form`), and an `http://` image.
  - The stock-Chromium self-test confirms Chromium reports `contained-mixed-form` for that page.
- **Downloads.** Attachment endpoints, plus slow bodies. A slow body holds its last chunk until the scenario releases it, so pause, resume and cancel always act on a live transfer.
- **Print.** `--kiosk-printing` with the Save as PDF destination seeded in `Preferences` writes the PDF into the seeded download folder without a dialog. This was verified on stock Chromium 155.

`openssl` must be installed. Node 22 or newer is needed for the built-in WebSocket client; the suite was written on Node 26.

## What each scenario automates

| Finding | Issue | Automated checks | Manual |
| --- | --- | --- | --- |
| F01 | #11 | Two split panes with 3 and 5 matches; counts, next/previous wrap and Escape dismissal per pane; DevTools, Inspect and Console each attach only to the active pane (CDP `attached`) and Toggle closes it; Print writes a PDF; Ctrl+F from page focus opens that pane's bar; Save Page is accepted without a crash; a background instance refuses DevTools but still finds | System print dialog; Save Page output (always asks for a path); docked layout, splitter, overlays and focus return; page-focus DevTools shortcuts |
| F02 | #12 | Isolated profile window has its own browser context, cookies, localStorage and history; host refuses a default-profile tab in an isolated window (`ERR_PROFILE_*`); shared-data profile shares cookies/storage and context; private window is a separate context, reads and leaks nothing, never reaches `linux-session.json`, and its data is gone after the last private window closes; a blocked profile directory leaves no phantom window; restart restores no private window and keeps regular data | Extension isolation; Settings panes per profile |
| F03 | #13 | Site panel text for HTTPS, HTTP, mixed, invalid certificate, HTTPS again, HTTP after HTTPS (no stale certificate) and `chrome://version`; web pages are never "Local Page" | Certificate viewer details |
| F23 | #33 | Four-page history; history menu jumps −2, +2, −3; zoom in matches `devicePixelRatio` and shows the `Zoom: 110%` indicator; the popover shows the percentage and its Reset returns to 100% and hides the indicator; zoom out and the reset command; per-site zoom listed by `sites.zoomLevels`, restored after restart with matching page scale, removed on reset | Pinch zoom |
| F07 | #17 | Status reports the store available, `backend: basic` and no reauthentication in a background instance; an empty vault lists `[]`; a credential saves through `ArdiaPasswords.save` and lists without its password; remove and never-save list work; a private profile name is refused with `ERR_PROFILE_PRIVATE` on generic and typed calls, and a credential saved in an isolated profile stays out of the default profile and the other way round; generic `passwords.reveal`/`passwords.save`/`autofill.cardNumber` are refused with `ERR_SECRET_ROUTE`; reveal, unlock, update and export are refused with `ERR_REAUTH_UNAVAILABLE`; addresses add, update and remove; a card saves through `ArdiaAutofill.saveCard`, lists only its last four digits and its number stays gated; password and autofill toggles change; the Passwords pane shows the basic-store notice; everything survives a restart; `Login Data`, `Web Data` and the browser log hold no fixture secret in plain text | polkit prompts (grant, deny, dismiss, timeout) for unlock, reveal, edit, export and card numbers; the five-minute grant window; a locked Secret Service; a private window's own renderer |
| F08 | #18 | Known file completes on disk and `fileStatus` sees it; Ctrl+J lists it; slow download progresses, pause stalls it, resume finishes it; cancel removes the partial file; remove forgets it in Chromium's history; the folder setting changes the destination; clear empties finished rows; restart shows the kept download once, nothing live, nothing cleared, no popover | Open, Show in Folder and Trash; ask-where-to-save dialog |
| F20 | #30 | Sidebar drop area found by its DOM `drop` listener; dropping one URL, several URLs and address-like text opens tabs; prose opens nothing; a CDP file drop (DOM-only paths) opens no `file:` tab; malformed `javascript:`/comment/NUL lists open nothing unsafe | Drag out of Downloads; real file-manager drops; internal reorder and page drops |
| F10 | #20 | With suggestions on, typed queries get the local endpoint's suggestions and requests carry no cookie (a `SID` cookie is planted for the host) or authorization; with suggestions off, zero requests and no remote rows; a private window gets suggestions without cookies | — |
| F22 | #32 | With sway or weston installed: the first window is active, Ctrl+N moves `document.hasFocus()` to the new window, a background window takes no focus, Settings comes to the front | Without a compositor, all of it; with one, immediate typing, restore-behind and links from other apps |
| F26 | #36 | Settings opens; no Engines pane; no Default browser, Open Ardia at login, Scroll bars or `ardia` command rows in General, Appearance or Advanced; no `ArdiaSystem`/`ArdiaApp`/unhandled rejections in the shells' console (replayed on attach) or the browser log | Default-browser and autostart across restart; streaming identity; header dragging; scroll bar style |
| F27 | #37 | Fresh profile: onboarding appears once, `onboarding.json` gets `startedAt`, the native launch record (`ArdiaShell.launchSteps`) lists all four steps and the startup renderer carries the `ardia.linux.app-launch` session mark; an unfinished onboarding resumes after restart without the intro; a completed one never returns; an existing two-window profile never onboards, the launch record is complete and exactly one renderer carries the mark, the profile is marked complete, and a reload of the owner does not onboard; Release Notes answers in its own window | Finishing onboarding through its steps; setup choices across windows; onboarding from a utility window |

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
- **Page-focus shortcuts.** Click into pane B's page and press Ctrl+Alt+I, Ctrl+Shift+J and Ctrl+Shift+C. DevTools opens on pane B each time.

**#12 F02**

- **Extensions.** An unpacked extension installed in the default profile is absent from the isolated profile, and the other way round.
- **Settings.** Settings opened from the isolated window shows only that profile's History, Passwords and Site Settings.

**#13 F03**

- **Certificate viewer.** Its issuer, validity and fingerprint match the served certificate.

**#17 F07**

- **System authentication.** Install `share/polkit-1/actions/dev.ardia.linux.policy` into `/usr/share/polkit-1/actions` and run a polkit agent. Unlock asks for your password and lists the passwords. Opening one within five minutes shows it without a second prompt; after five minutes it prompts again. Export CSV… shows the save dialog, then always prompts, and writes the file with mode 0600. Showing a card number always prompts.
- **Refusals.** Deny the prompt, dismiss it, and leave it open for two minutes. Each time the password stays hidden, Edit doesn't save, Export writes nothing, and the card number stays masked.
- **Locked keyring.** With `--password-store=gnome-libsecret` and a locked or absent Secret Service, the Passwords pane shows "Passwords are unavailable" with the keyring reason, not an empty vault.
- **Private windows.** A private window's own renderer gets `ERR_PROFILE_PRIVATE` for every passwords and autofill call and never shows the regular profile's data.

**#18 F08**

- **Open, Show in Folder and Trash.** In a desktop session:
  - Clicking a finished row opens the file.
  - Show in Folder selects it in the file manager. Check each path: portal, FileManager1 and `xdg-open`.
  - Move to Trash puts it in `~/.local/share/Trash/files` with a `.trashinfo` record.
- **Ask where to save.** With "Ask where to save" on, downloading shows the native dialog. Cancelling leaves no live row.

**#30 F20**

- **Drag out.** Drag a finished download into a file-manager folder, on Wayland and on X11. The file copies, the press alone does not open it, and releasing before the drag starts does nothing.
- **Drop in.** Drag one, then three files from the file manager onto the sidebar and onto the top strip. Each opens as a `file://` tab, and the drop indicator shows and clears.
- **Existing drags.** Sidebar reorder, tab-to-page split and link-to-text-field drops still work.

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

**#37 F27**

- **Onboarding.** Finish onboarding through every step. It doesn't return after a restart, and Help › Show Onboarding replays it.
- **Setup choices.** Choices made in onboarding (search engine, top tab layout, two pins) appear in a second window and survive a restart without duplicate pins.
- **Utility windows.** Show Onboarding and Tool Tour run from Settings play in a browser window.

## Notes for the first real run

The scenarios drive the shared UI through the same seams the existing keyboard test uses:

- `ardiaLinux.store`, `ardiaLinux.emitCommand`;
- `__ardiaHost.invoke` and `__ardiaHost.call`;
- `[data-ardia-linux-window]`.

They also use visible labels from the shared components: `Site Controls`, `Go Back`/`Go Forward`, `Zoom: N%`, `Find on Page`, `Reset`, `Skip` and the Settings pane names.

If a scenario fails because a label or flow changed rather than because the feature is broken, the failing check's `detail` says what it waited for. The kept scratch profile has the browser log for the first real triage.
