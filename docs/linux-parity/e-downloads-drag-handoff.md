# Lane E handoff: Downloads and file drag/drop (#18 F08, #30 F20)

Branch `t3/linux-downloads-drag`, rebased onto `linux` at `52d72e78` (after #67). Source-level implementation with compiled fixtures and Linux host tests. It has not been built into Chromium 156 or run natively.

## What was done

### Downloads (F08)

The shared `ac_downloads_*` engine service already observes each profile's `DownloadManager` and emits `downloads.changed` and `downloads.removed`. On Linux nothing ever created that observer: it starts only on the first `ac_downloads_list` call or download-ID lookup, and Linux made neither. The host also didn't route `keep`, and had no file open/reveal/trash backend. `ArdiaSystem.fileExists` resolved to an async host method, so the synchronous shared check always got a truthy Promise.

- `engine/ardiacore-linux/src/ardia/linux/ac_linux_contract.h`: routes `downloads.keep` to the shared engine and forwards `downloads.removed` as a domain event.
- `downloads.list`, `downloads.remove` and `downloads.clear` are handled by the Linux host itself (`LinuxDownloadsCall` in `ac_linux_files.cc`, logic in `ac_linux_download_service.{h,cc}`). Nothing in `engine/chromium` changes, so macOS is unaffected.
  - **list** waits until the regular profile's `DownloadManager` reports `IsManagerInitialized()`, so restored history is complete, then calls the shared `ac_downloads_list`. That call also starts the shared live-event observer, which by then sees history items as existing items. The reply is filtered natively to drop off-the-record and malformed entries (`FilterSnapshot`). The wait gives up as unavailable on `ManagerGoingDown`, and after 10 s it replies with whatever history has loaded.
  - **remove** removes one listed, non-in-progress item, including a resumable interrupted one, because `DownloadItemImpl::Remove()` interrupts and discards first.
  - **clear** also waits for history, then removes every listed, non-in-progress item that started at or before `before` and isn't in `keep` (at most 100 GUIDs).
  - "Listed" applies the same filter as the shared `Listed()`: not transient, not an extension-installer download, no webstore approval, not a trusted CRX download.
- `ac_linux_downloads.{h,cc}` contains pure helpers, with no Chromium dependency beyond `base/values.h`:
  - `ValidDownloadId`: a canonical 8-4-4-4-12 hex GUID.
  - `ValidLocalPath`: absolute, at most 4096 bytes, no NUL, no empty, `.` or `..` components, no trailing slash.
  - `ValidateRequest`: control calls (`cancel`, `pause`, `resume`, `keep`, `remove`) take exactly `{id}`; `list` and `settings` take `{}`; `clear` takes exactly `{keep: GUID[] ≤ 100, before: ms}`; `setSettings` takes only an absolute `folder` and/or a boolean `askWhereToSave`.
  - `FilterSnapshot`, `KeptIds` and `ClearCutoff`.
  - `IconPathAllowed`: bare file names, or listed download targets.
  - `ListedPaths`.
  - `ChooseOpenBackend` and `ChooseDragStart`.
  - `kFileCapabilities`.
  - `FileUrl`: percent-encoded `file://` URLs.
  - `ChooseRevealBackend`.
  - `PathListed`.
  - A freedesktop Trash implementation (`TrashDirectory`, `MoveToTrash`). It creates the `.trashinfo` record with `O_EXCL` first, then uses `link`+`unlink`, so it never overwrites an existing file, including orphans in `files/`. It refuses symlinks, non-regular files and other filesystems.
- The host rejects any invalid `downloads.*` request with `ERR_DOWNLOAD_REQUEST` before it reaches `ac_engine_call`.
- `apps/linux/src/hostDownloads.ts` (`startHostDownloads`, started in `main.tsx` for browser windows):
  - **Startup snapshot:** calls `downloads.list` once.
    - It validates every entry and appends only unknown IDs after live entries, newest first, within the shared cap of 100.
    - IDs removed while the list request was in flight are excluded, whether locally or by a `downloads.removed` event.
    - It doesn't call `upsertDownload`, so the downloads popover doesn't open.
    - New snapshot entries are first committed under a profile no window shows, then under the regular profile. The shared `watchNewDownloads` therefore records them as already seen and doesn't play the new-download animation.
    - If history holds more than the visible 100, it calls `downloads.clear` with the visible IDs as `keep` and the request time as `before`. That prunes the hidden finished history, so a later removal can't surface older entries on the next start.
  - **Remove:** removes the entry from Chromium's download history.
  - **Clear:** calls `downloads.clear` with the still-listed IDs as `keep`, which removes every finished entry the engine knows, including ones never shown.
  - **Other windows:** a `downloads.removed` event from another window removes the entry here through the original action, so it isn't echoed back to the engine.
- `apps/linux/src/hostFiles.ts` (the `ArdiaSystem` adapter, resolved in `hostModules.ts`):
  - `fileExists`: synchronous, answered from a cache. Native `ArdiaSystem.fileStatus` lookups are batched (16 ms, at most 64 paths) and refreshed after 2 s. When an answer changes, the downloads list is re-rendered.
  - `openFile`, `revealFile`, `moveToTrash`: return booleans; native errors are logged with their codes.
  - `fileIcon`: accepts only PNG data URLs.
  - `appIcon`: returns `null`.
- Native `ArdiaSystem.*` (in `ac_linux_files.cc`):
  - `openFile`, `moveToTrash` and file drags work only on **completed, non-dangerous** listed download targets of the window's regular profile. `revealFile` and `fileStatus` accept any listed target; other paths fail with `ERR_FILE_NOT_ALLOWED` or, for `fileStatus`, report `false` without being checked on disk.
  - `fileIcon` accepts a bare file name or a listed target.
  - Open first checks that `xdg-open` is on `PATH` and fails with `ERR_OPEN_UNAVAILABLE` if it isn't. On Linux, `platform_util::OpenItem(OPEN_FILE)` always posts `xdg-open` and reports `OPEN_SUCCEEDED` (`platform_util_linux.cc` `PlatformOpenVerifiedItem`); other failures map to their own codes.
  - Reveal uses `platform_util::ShowItemInFolder` (portal, then FileManager1 `ShowItems`, then `xdg-open` on the parent folder). It fails with `ERR_REVEAL_UNAVAILABLE` when there is neither a session bus nor `xdg-open`, and with `ERR_FILE_MISSING` when the file is gone.
  - Icons come from `IconManager` as PNG data URLs.

### File drag and drop (F20)

- **Inbound:**
  - `LinuxShellWebView` replaces the shell's plain `views::WebView` and overrides `CanDragEnter`. When an OS-level drag enters the shell, it records the dropped file paths in a host-owned `LinuxDropLedger`. Renderer-originated drags have no filenames, because Chromium's `FilterDropData` clears them.
  - The ledger holds one drop at a time, at most 64 valid absolute paths, expiring after 120 s. It can be claimed once.
  - `ArdiaFileDrop.takeDrop(names)` claims it only when the DOM file names match the recorded basenames, and returns `file://` URLs.
  - The renderer never supplies inbound file paths.
- `apps/linux/src/shims/web/FileDropArea.tsx` (exported from `shellBase.tsx` in place of the shared placeholder view):
  - Emits the shared `DropUpdate` sequence (`enter`/`move`/`exit`/`drop`) with element-relative coordinates.
  - URLs and text come from the DOM (`text/uri-list`, then `text/plain`). `file:` entries in the DOM are ignored, so a file item exists only if the host issued it.
  - A nested area claims the event, and an outer area treats that as an exit.
  - A document-level guard stops stray drops from navigating the shell, and leaves editable fields alone.
- **Outbound:**
  - `MouseArea` with a `path` (finished downloads in the shared UI) starts `ArdiaFileDrag.begin(path)` after 4 px of primary-button movement, then suppresses the click that follows.
  - Native code checks that the path is a completed download and a regular file. In the reply task, after that file check, it starts only if the window is alive, no drag is active and `aura::Env::IsLeftMouseButtonDown()` is still true.
  - Otherwise it fails with `ERR_WINDOW_CLOSED`, `ERR_DRAG_ACTIVE` or `ERR_DRAG_RELEASED`. `MouseArea` doesn't log the released case.
  - When it starts, it replies `true` and runs `Widget::RunDragDropLoop` with `OSExchangeData::SetFilename`. `WidgetDeletionObserver` guards the loop against the window closing.
- **Capabilities:** `ArdiaSystem.fileStatus`, `ArdiaFileDrop.takeDrop` and `ArdiaFileDrag.begin` are advertised in bootstrap capabilities from `kFileCapabilities`. The JS gates use the same names from `fileCapabilities.ts`, and a test checks the two match. An older native host therefore gets no drag attempts, no file claims, and `false` from `fileExists`.

## Contract summary

| Call / event | Arguments | Reply |
| --- | --- | --- |
| `downloads.list` | `{}` | `Download[]` (shared `Describe` shape, regular profile only, after history loads) |
| `downloads.{cancel,pause,resume,keep,remove}` | `{id: GUID}` | `null` |
| `downloads.clear` | `{keep: GUID[] ≤ 100, before: ms}` | number removed |
| `downloads.settings` / `downloads.setSettings` | `{}` / `{folder?, askWhereToSave?}` | `DownloadSettings` |
| event `ArdiaDownloads`/`downloads` `downloads.changed` | `Download` with `profile: ""` | |
| event `ArdiaDownloads`/`downloads` `downloads.removed` | `{id, profile: ""}` | |
| `ArdiaSystem.fileStatus` | `[paths ≤ 64]` | `boolean[]` (`false` for paths that aren't listed downloads) |
| `ArdiaSystem.openFile` / `revealFile` / `moveToTrash` | `[path]` | `true`, or error `{code, message}` |
| `ArdiaSystem.fileIcon` | `[name, size]` | PNG data URL or `null` |
| `ArdiaFileDrop.takeDrop` | `[names ≤ 64]` | `file:///…` URLs, or `ERR_NO_FILE_DROP` |
| `ArdiaFileDrag.begin` | `[path]` | `true` once the drag starts, or `ERR_DRAG_RELEASED`, `ERR_DRAG_ACTIVE`, `ERR_WINDOW_CLOSED` or `ERR_FILE_MISSING` |

## Tests run

Round 2 (after review round 1):

- `pnpm --filter @ardia/linux typecheck`: passes.
- `pnpm --filter @ardia/linux test:host`: 224 passed, 0 failed.
- `pnpm --filter @ardia/linux build`: passes.
- `python3 -m unittest test_downloads test_contract`: 6 passed. Each `test_downloads` test compiles and runs real code under ASan and UBSan:
  - the pure helpers, with a scenario fixture;
  - `ac_linux_download_service.cc`, against stub `DownloadManager` and `DownloadItem` interfaces: the history wait, timeout, going down, clear with keep and cutoff, single removal, and the `Listed()` predicate;
  - `ac_linux_contract.h`;
  - a check that the GN source list matches the source directory;
  - a check that the native capability list matches the JS gates.
- Full `python3 -m unittest discover -s engine/ardiacore-linux/tests`: 210 tests, all passing (the five close-fixture failures from before are fixed by #67).
- `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`: 7 passed.
- With the Chromium 156 toolchain flags, a syntax-only compile passes for `ac_linux_downloads.cc`, `ac_linux_file_drop.cc` and `ac_linux_download_service.cc` against the real `DownloadManager`, `DownloadItem`, timer and observation headers. `ac_linux_files.cc` and the host hunks still could not be syntax-checked: the 156 out directory lacks generated mojom headers such as `content/public/common/extra_mojo_js_features.mojom.h`. Their API use was checked by hand against the 156 headers.

## Still needs native runtime acceptance on a Chromium 156 build

I could not build or run Chromium here. These still need checking on a real build:

- The host and `ac_linux_files.cc` compile and link, including `gn check` for the new deps (`//chrome/browser:{browser_process,icon_loader,icon_manager,primitives}`, `//chrome/browser/download`, `//extensions/browser`, `//components/download/public/common`, `//ui/display`, `//ui/base/clipboard:file_info`, `//ui/base/dragdrop/mojom`).
- A known download shows progress and completion in Ctrl+J and in the popover, and pause, resume, cancel, keep and discard work.
- After a restart, history entries appear exactly once, including when `downloads.list` runs before history has loaded. Cleared ones stay gone, history beyond 100 is pruned, and nothing reports as "live" when it isn't.
- Open, Show in Folder (portal, FileManager1 and `xdg-open` paths) and Move to Trash act on the selected file. Show in Folder reports `ERR_REVEAL_UNAVAILABLE` with no desktop.
- Changing the destination folder and "ask where to save" changes Chromium's behavior. The Settings folder picker relies on `ArdiaShell.pickFolder`, which is still optional and unimplemented on Linux (F05/F26).
- `CanDragEnter` records files from Wayland and X11 file managers, and the DOM `files` names match the recorded basenames. Dropping files, URLs, text and several items at once onto the sidebar and top strip opens tabs. Internal reorder and drops onto web pages are unchanged.
- Dragging a finished download into a scratch application works on Wayland (start_drag serial) and X11. The press does not also open the file. On X11 and Wayland, a release before the native check returns `ERR_DRAG_RELEASED` and no drag starts.

## Open risks

- Startup pruning deletes finished Chromium download-history entries older than the newest 100. The shared store already caps Ardia's list at 100 and never shows the rest, but they also disappear from `chrome://downloads`. I chose this so removals can't bring hidden history back.
- If history takes longer than 10 s to load, `downloads.list` returns whatever has loaded by then.

- `fileExists` answers `false` until the first status reply (about 16 ms plus IPC), so a finished row can briefly render as missing.
- Reveal availability is a heuristic. With a session bus but neither a portal, FileManager1 nor `xdg-open`, Chromium's fallback fails silently.
- Trash only supports the home trash. A download on another filesystem fails explicitly instead of using `$topdir/.Trash-$uid`.
- An inbound claim fails, falling back to DOM URLs and text only, if Chromium's DOM file names ever differ from the recorded path basenames.
