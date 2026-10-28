# Linux page, media and system follow-ups (lane page-2)

Branch `t3/linux-page-followups-2`. Source-level work for #124, #154, #156, #168, #169 and #170. No Chromium 156 GN, compile, link or runtime run was possible in this lane. Everything below is source-level, fixture-level and 156-header syntax-check evidence. The three Linux-only Chromium patches (autoplay policy, Web Store uninstall, media-player PiP) apply with zero fuzz to the 156 tree, and the patched Chromium files pass a 156 syntax check with their own ninja flags.

## What changed

### Memory pressure from PSI (#170)

- `ac_linux_memory_pressure.{h,cc}` (POSIX only, fixture-tested) watches pressure stall information. It reads the process's own cgroup v2 `memory.pressure` (found through `/proc/self/cgroup`) and the system `/proc/pressure/memory`, and reports the higher level of the two.
- On procfs and cgroup2 each file gets a kernel trigger, `some 150000 2000000`. Unprivileged processes may arm a 2 s window since Linux 6.4. A dedicated blocking thread in `LinuxPowerMonitor` polls the triggers for `POLLPRI`. If a file is on another file system, or the kernel refuses the trigger, it is sampled every 5 s.
- Levels come from stall time measured between two samples:
  - warning at 10% `some`;
  - critical at 10% `full` or 40% `some`;
  - critical lasts until `full` drops under 5% and `some` under 20%; warning lasts until `some` drops under 5%.
- How sampling works:
  - The first sample uses `avg10`.
  - A trigger sets the level to at least warning and measures again 1 s later.
  - While the level is above normal it is measured every 2 s.
  - A window shorter than 0.5 s, or a counter that goes backwards, keeps the current level.
- If a source stops reading (for example its cgroup is removed), it is dropped. Three `poll` failures in a row (not counting EINTR) drop PSI altogether, and a failed or empty wait sleeps instead of spinning. With no PSI source at all, the old MemAvailable ratio is sampled every 30 s.
- `engine.systemState` adds `memorySignal: "psi" | "meminfo"`.

### Show in Folder and volume trash (#156)

- `RevealRequest` (`ac_linux_files.cc`) replaces `platform_util::ShowItemInFolder`. It follows the fixture-tested `LinuxRevealChain`, which ignores late answers from a route it has already left, and tries three routes in order:
  1. the portal's `OpenURI.OpenDirectory`, given the file's fd (opened with `O_NONBLOCK | O_NOFOLLOW`, regular files only) and an activation token; a request the user cancels counts as handled, and no answer within 10 s moves on to the next route;
  2. `FileManager1.ShowItems`, probed when this route is reached;
  3. `xdg-open .` in the file's folder, waited on for 5 s; exit 0, or still running, counts as success.
- When every route it tried fails, the call returns `ERR_REVEAL_FAILED` with a message naming those routes. With no route at all it is still `ERR_REVEAL_UNAVAILABLE`.
- The trash now works through handles instead of paths:
  - Each trash directory is opened once with `O_DIRECTORY | O_NOFOLLOW`, and its ownership and mode are checked with `fstat`.
  - The record and the link are made with `openat`/`linkat` relative to those handles. This applies to the home trash, the shared sticky `.Trash/<uid>` and `.Trash-<uid>`.
  - The file is linked from a handle on its own folder. Both the new link and, just before the final `unlinkat`, the source name are compared by device and inode with the file that was checked; if either differs the move is undone with `ERR_FILE_CHANGED`.
  - The reported path is read back from `/proc/self/fd`.
  - On a top directory that others can write and that isn't sticky, renaming or planting `.Trash-<uid>` after the check changes nothing: the file still goes into the directory that was checked.

### Manual PiP outside the media session (#124)

- `RouteLinuxPictureInPicture` returns `kElement` when a verified control press finds no session video.
- `chromium-linux-media-player-pip.patch` (Linux only) adds two content helpers, declared in `ac_linux_content_hooks.h`:
  - `ArdiaLinuxPictureInPictureCandidates(WebContents*)` lists the primary main frame's players that have video, a bound `media::mojom::MediaPlayer` remote and Blink's PiP availability (`SupportsPictureInPicture`, which covers metadata, a video track and `disablePictureInPicture`), each with whether it is playing.
  - `ArdiaLinuxRequestPictureInPicture(WebContents*, MediaPlayerId)` calls that player's `RequestEnterPictureInPicture`. Blink enters PiP for it without a user gesture, the same path as auto-PiP and the context menu.
- `LinuxPageMedia` tracks each player's size from `MediaResized`. `ChooseLinuxPictureInPicturePlayer` takes playing players first, then the largest, then the first listed.
- Nothing grants the page user activation. The earlier route, which notified activation and consumed only transient activation, left sticky activation set; that lifted a per-site autoplay block and enabled beforeunload prompts. It is gone, and so are its mojom methods and agent scripts.
- The request shares the session route's pending slot and its 1.5 s deadline.

### Main-world shims (#168)

- **Autoplay.**
  - The page channel's config now carries `blockAutoplay` when the top-level page's origin has `ContentSettingsType::AUTOPLAY` set to block in the page's own profile (http(s) only).
  - The page script's pause works from the isolated world, because DOM nodes and their `play` events are shared between worlds.
  - A change to a site's Autoplay setting now applies to its open tabs. `LinuxAutoplayFollower` watches the profile's content settings, calls `WebContents::OnWebPreferencesChanged()` (which recomputes the autoplay policy through the hook below) and re-sends the page config.
  - `chromium-linux-autoplay-policy.patch` adds `g_ardia_linux_autoplay_blocked` to `GetAutoplayPolicyForWebContents`. For a blocked site it returns `kDocumentUserActivationRequired` and skips the media-engagement, enterprise-policy and mic/camera bypasses. That covers media the script cannot see, such as an `Audio` object that is never attached to the document.
- **Notification** uses Chromium's own notification service and Linux bridge, with permissions from the same content settings.
- **mediaSession** is observed natively by `LinuxPageMedia`.
- **getDisplayMedia** uses Chromium's portal picker. `displayMediaPicker` is never set.
- The channel still refuses `notification`, `notificationClose`, `displayMedia`, `nowPlaying` and `pipTrace`, so the isolated-world copies of these shims cannot do anything.

### Web Store bridge and popup size (#169)

- `chromium-linux-web-store-uninstall.patch` adds `g_ardia_linux_store_uninstall` to `ChromeManagementAPIDelegate::UninstallFunctionDelegate`.
- `ac_linux_web_store.cc` takes the call only when all of these hold: the caller is a web page, not an extension; it is on the Web Store domain; and it is in a tab an Ardia window holds. Every other call keeps Chromium's dialog.
- The request is queued on the tab (`LinuxWebStoreQueue`: at most 16, extension ids only). The Linux adapter maps the shared `STORE_SCRIPT` literal to the `webStoreRequest` tab command, which answers the next request's extension id. The shared bridge then runs Ardia's own removal flow.
- One wait is open per tab:
  - A newer wait answers the older one with `{superseded: true}`. The adapter turns that into a promise that never settles, so the bridge does not listen again.
  - Only a page change, or the tab closing, answers null.
  - A failed command answers null only after 1 s, so a tab that is gone cannot make the bridge spin.
- The store page's `chrome.management.uninstall` call resolves only once the extension is really uninstalled.
- It reports the uninstall as cancelled 30 s after Ardia's confirmation closes without a removal. While an owned dialog is open in the tab's window, the countdown is renewed every second. It also reports cancelled when the page changes or the tab closes.
- On store pages the host runs a fixed relabel script in Ardia's isolated world. It changes "Add/Remove from Chrome" to "Add/Remove from Ardia".
- Popups: `HostExtensionWebView` answers the shared `MEASURE` literal from the native `preferredSize` and answers any other script with null. The host refuses `evaluate` on extension views (`ERR_PAGE_EVALUATION_REFUSED`).

### Typeahead with input methods (#154)

- `TypeaheadInput` (`ac_linux_typeahead_input.{h,cc}`) holds keys before the input method sees them. It is a `ui::EventRewriter` on the window's host, and the existing ownership rule is checked against the focused window.
- While it is capturing, it is the IME's focused `TextInputClient`, with type `NONE`. If another client takes IME focus meanwhile, it takes the focus back only when the focused window belongs to this window's widget. A dialog or other widget keeps its own IME focus and typing.
- Release removes the rewriter, gives IME focus back to the last client of this window that asked for it, and returns the raw keys. The existing replay sends them through `InputMethod::DispatchKeyEvent`, so the IME processes each key exactly once.
- With a compositor-side IME on Wayland, keys typed before the field was ready replay as the typed characters.

## Contract

| Item | Contract |
| --- | --- |
| `engine.systemState` | adds `memorySignal` (`psi` or `meminfo`) |
| `ArdiaSystem.revealFile` | `ERR_REVEAL_FAILED` with the routes that were tried; `ERR_REVEAL_UNAVAILABLE` when no route exists |
| `ArdiaSystem.moveToTrash` | adds `ERR_FILE_CHANGED` |
| Content hooks | `ArdiaLinuxPictureInPictureCandidates(WebContents*)`, `ArdiaLinuxRequestPictureInPicture(WebContents*, MediaPlayerId)` |
| Page config | `blockAutoplay: true` when the top-level origin's autoplay is blocked |
| Tab command `webStoreRequest` | the next Web Store uninstall id; `{superseded: true}` when a newer wait starts; `null` when the page changes or the tab closes |
| `extensionView.command evaluate` | `ERR_PAGE_EVALUATION_REFUSED` |
| Chromium hooks | `g_ardia_linux_autoplay_blocked(WebContents*)`, `g_ardia_linux_store_uninstall(ManagementUninstallFunctionBase*, const Extension*)` |

## Tests run

| Command | Result |
| --- | --- |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | see the PR description |
| `test_memory_pressure`, `test_media_policy`, `test_power_bus` | pass |
| `test_downloads` | 6 pass |
| `test_element_picture_in_picture`, `test_autoplay_policy`, `test_page_messages`, `test_web_store`, `test_typeahead`, `test_keyboard_input` | pass |
| `scripts/linux/syntax-check-156.sh` on every changed native file | pass |
| `pnpm --filter @ardia/linux typecheck`, `test:host`, `test:graph`, `build`, `acceptance:self-test` | see the PR description |

Each new or changed case failed before its fix, either because it did not compile or because an assertion failed.

## Needs native runtime acceptance on a Chromium 156 build

- GN, compile and link of the new sources and the three patches. The patched Chromium files pass a syntax check; nothing has been linked.
- PSI on a real stall: `stress-ng --vm 4 --vm-bytes 95%` should report warning within 2 s and go back to normal a few seconds after the load stops.
- Each reveal route failing in a desktop session, and W156.
- Element PiP for a muted video, and the activation being consumed afterwards.
- Autoplay blocked on a site (F15's new check) and allowed again after a reset.
- Remove from Ardia on the Web Store, and popups that change size.
- Ctrl+N followed by composed input with ibus, with fcitx5, and with Wayland text-input.

## Left out

- Text from a virtual keyboard or voice input committed to a loading window is not held: nothing in it is a key event.
