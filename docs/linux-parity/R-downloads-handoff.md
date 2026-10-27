# Lane R handoff (2 of 2): downloads, DevTools, extension prompt and tooling follow-ups

Branch `t3/linux-downloads-followups`, based on `linux`. Source-level fixes for [#74](https://github.com/mckamyk/ardia/issues/74), [#75](https://github.com/mckamyk/ardia/issues/75), [#76](https://github.com/mckamyk/ardia/issues/76), [#137](https://github.com/mckamyk/ardia/issues/137) and [#80](https://github.com/mckamyk/ardia/issues/80), tracked by [#141](https://github.com/mckamyk/ardia/issues/141). The window and session half is in [R-window-handoff.md](R-window-handoff.md).

None of the native code has been compiled into Chromium 156 or run. `ac_linux_files.cc` and `ac_linux_downloads.cc` pass `clang -fsyntax-only` against the 156 tree's headers with its own `linux.ninja` flags (read only). The host and `ac_linux_extension_service.cc` hunks add no errors; both files already fail on `linux` for unrelated reasons ([#157](https://github.com/mckamyk/ardia/issues/157)).

## #80: `scripts/agent/locked` on Linux

On Linux the wrapper holds the same `/tmp/ac-<name>.lockf` files with util-linux `flock -o -w <secs> -E 75` (the lock belongs to the flock process and dies with it, as with `lockf`), and uses GNU `sed -i`, `date -d @`, `stat -c` and `sha1sum`. Holder files, the legacy mkdir directory, takes, `--as`, nested calls (the inner call recognises a `flock` parent) and exit 75 on timeout behave as on macOS. macOS keeps the exact `lockf -s -k -t` invocation. `scripts/linux/build.sh`'s own `flock` on `ac-chromium.lockf` shares the lock.

## #74: download folder picker

**Contract.** `ArdiaShell.pickFolder(current: string|null, prompt: string|null)` (advertised capability):

- opens Chromium's `SelectFileDialog` in folder mode (portal, GTK or KDE, as Chromium picks), owned by the calling window and seeded with `current` when it is a valid absolute path;
- on a pick, checks the folder on a blocking task (`CheckDownloadFolder`: valid absolute path, exists, is a directory, writable and searchable) and replies with its path, or rejects with `ERR_FOLDER_MISSING`, `ERR_FOLDER_TYPE`, `ERR_FOLDER_NOT_WRITABLE` or `ERR_FILE_PATH`;
- replies `null` on cancel, and in background instances (which log the request instead of showing a dialog);
- rejects other argument shapes with `ERR_FOLDER_REQUEST`.

The Linux shell answers `pickFolder` locally (`hostFolders.ts`): a rejected folder is explained in an owned confirm dialog offering "Choose Another Folder…" (Settings renders no toasts); a host without the picker says so. Settings then applies the folder through the existing `downloads.setSettings`.

## #75: file state, reveal and trash

- **No transient missing state.** The downloads adapter marks finished downloads, from live `downloads.changed` events and the startup list, as expected in the file-status cache (`HostFiles.expect`), so their rows read as present at once; the native check then corrects only a really missing file. Paths that aren't downloads (onboarding's app checks) are unaffected.
- **Reveal failures surface.** Before Show in Folder, the host asks D-Bus whether `org.freedesktop.portal.Desktop` or `org.freedesktop.FileManager1` is running or activatable (`dbus_utils::CheckForServiceAndStart`, the probe Chromium uses), and whether `xdg-open` exists with a folder handler (`xdg-mime query default inode/directory`). With none, it fails with `ERR_REVEAL_UNAVAILABLE` instead of letting Chromium fall through silently. Failed open, reveal and trash actions now show a toast in the window ("Couldn't Show the File" and so on); the downloads adapter wires that notice, because the file adapter sits in the Expo module chain and must not import shared state.
- **Per-volume trash.** A file on another file system than the home trash goes to its volume's trash, per the freedesktop spec: `$topdir/.Trash/$uid` when `$topdir/.Trash` is a sticky directory and not a symlink, otherwise `$topdir/.Trash-$uid`. The file's directory is resolved first, the top is the last ancestor on the same device, every trash directory and its `files`/`info` must be real directories owned by the user with no group or other access (planted symlinks are refused), and the record's `Path` is relative to `$topdir`.

Left: portal or FileManager1 calls that fail inside Chromium still fall back to `xdg-open` silently, and a top directory others can write is open to a swap race ([#156](https://github.com/mckamyk/ardia/issues/156)).

## #76: docked DevTools before the first viewport

DevTools asked for on a pane with no viewport yet was docked by Chromium into its own `ContentsContainerView` under the overlay, where it could take keyboard focus unseen. Without a viewport, `LayoutPane` now takes the current claim's docked frontend out of Chromium's container and hides it. The command is never rejected: the first `tab.viewport` lays out the pane, shows the same frontend in its DevTools view and focuses it as new.

The panel keeps the page's 12 px corner radius. `docs/dia-spec.md` has no Dia DevTools measurement ([#155](https://github.com/mckamyk/ardia/issues/155)).

## #137: install prompt and its parent tab

A prompt with a parent WebContents carries a `PromptParentWatch`. When the parent is destroyed, the watch stops observing and posts `AbortPromptForDestroyedParent`, which takes the prompt from the ledger (a later answer is stale), sends `extensions.installPromptCancelled {requestId}` to the window holding it (after any retarget), and answers Chromium `ABORTED`. Answering, retargeting or refusing the prompt drops the watch with its entry. Prompts without a parent tab keep the window-bound behaviour. The Linux UI (`extensionPrompts.ts`) closes the install dialog when the cancellation names the prompt it shows, without answering it again.

## Tests

| Command | Result |
| --- | --- |
| `pnpm --filter @ardia/linux typecheck` | passes |
| `pnpm --filter @ardia/linux test:host` | all pass (counts in the PR body) |
| `pnpm --filter @ardia/linux test:graph` | 3 pass |
| `pnpm --filter @ardia/linux build` | passes |
| `pnpm --filter @ardia/linux acceptance:self-test` | all pass |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | all pass (counts in the PR body) |
| `python3 -m unittest discover -s engine/patches/build/tests` | all pass, including `test_agent_locked.py` (10) |

New or extended: `test_agent_locked.py` (run, exit status, busy timeout naming the holder, release when the holder dies, nested calls, take/renew/as/release, expired takes, legacy holders, derived-data names, the exclusive Pods lock); the downloads fixture (reveal choice, sticky and fallback volume trashes, symlink refusal, relative records, a resolved symlinked folder, a real cross-mount move through `/dev/shm` when it is a separate mount, folder checks); `test_pane_layout.py`'s before-viewport case on the real `LayoutPane`; the compiled extension-service scenarios now run the real watch and abort against a fake `WebContentsObserver`; `hostFiles`, `hostDownloads`, `hostFolders` and `extensionPrompts` host tests. Each was failing before its fix.

## Acceptance scenarios

`apps/linux/scripts/acceptance/scenarios/r-downloads-followups.mjs`: W74 (picker advertised, background instance returns no folder, malformed requests refused), W75 (with `PATH` emptied, reveal reports `ERR_REVEAL_UNAVAILABLE`; a download on `/dev/shm` goes to `/dev/shm/.Trash-<uid>` when it is a separate mount), W76 (DevTools requested in the same task as the tab's creation opens and inspects it; screenshot evidence), W137 (manual: needs the Web Store).

## Needs native runtime acceptance (not done here)

I could not build or run Chromium 156. On a build:

1. W74, W75, W76 and W137 in the acceptance suite.
2. The real folder dialog on GNOME/KDE/Hyprland: pick, cancel, an unwritable folder (W74 manual).
3. Show in Folder with a portal and with FileManager1; a finished row never flashing as missing (W75 manual).
4. A Web Store install prompt dismissed by closing its tab (W137 manual).
