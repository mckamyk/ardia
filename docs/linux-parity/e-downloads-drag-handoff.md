# Lane E handoff: Downloads and file drag/drop (#18 F08, #30 F20)

Branch `t3/linux-downloads-drag`, based on `linux` at `4b3d21e0`. Source-level implementation with compiled fixtures and Linux host tests. It has not been built into Chromium 156 or run natively.

## What was done

### Downloads (F08)

The shared `ac_downloads_*` engine service already observes each profile's `DownloadManager` and emits `downloads.changed` and `downloads.removed`. On Linux nothing ever created that observer: it starts only on the first `ac_downloads_list` call or download-ID lookup, and Linux made neither. The host also didn't route `keep`, and had no file open/reveal/trash backend. `ArdiaSystem.fileExists` resolved to an async host method, so the synchronous shared check always got a truthy Promise.

- `engine/ardiacore-linux/src/ardia/linux/ac_linux_contract.h`: adds the routes `downloads.keep`, `downloads.remove` (`null` replies) and `downloads.list` (the `downloads` field), and forwards `downloads.removed` as a domain event.
- `ac_linux_downloads.{h,cc}` contains pure helpers, with no Chromium dependency beyond `base/values.h`:
  - `ValidDownloadId`: a canonical 8-4-4-4-12 hex GUID.
  - `ValidLocalPath`: absolute, at most 4096 bytes, no NUL, no empty, `.` or `..` components, no trailing slash.
  - `ValidateRequest`: control calls (`cancel`, `pause`, `resume`, `keep`, `remove`) take exactly `{id}`; `list` and `settings` take `{}`; `setSettings` takes only an absolute `folder` and/or a boolean `askWhereToSave`.
  - `FileUrl`: percent-encoded `file://` URLs.
  - `ChooseRevealBackend`.
  - `PathListed`.
  - A freedesktop Trash implementation (`TrashDirectory`, `MoveToTrash`). It creates the `.trashinfo` record with `O_EXCL` first, then uses `link`+`unlink`, so it never overwrites an existing file, including orphans in `files/`. It refuses symlinks, non-regular files and other filesystems.
- The host rejects any invalid `downloads.*` request with `ERR_DOWNLOAD_REQUEST` before it reaches `ac_engine_call`.
- `apps/linux/src/hostDownloads.ts` (`startHostDownloads`, started in `main.tsx` for browser windows):
  - **Startup snapshot:** calls `downloads.list` once. This also starts the engine observer. It validates every entry, drops off-the-record ones and appends only unknown IDs after live entries, newest first, within the shared cap of 100. It doesn't call `upsertDownload`, so restart doesn't pop open the downloads popover.
  - **Remove/Clear:** wraps the store's `removeDownload` and `clearDownloads`. Finished, cancelled and failed entries with native GUIDs are also removed from Chromium's download history, so a restart cannot bring them back. A failed download that is still resumable is cancelled first.
  - **Other windows:** a `downloads.removed` event from another window removes the entry here through the original action, so it isn't echoed back to the engine.
- `apps/linux/src/hostFiles.ts` (the `ArdiaSystem` adapter, resolved in `hostModules.ts`):
  - `fileExists`: synchronous, answered from a cache. Native `ArdiaSystem.fileStatus` lookups are batched (16 ms, at most 64 paths) and refreshed after 2 s. When an answer changes, the downloads list is re-rendered.
  - `openFile`, `revealFile`, `moveToTrash`: return booleans; native errors are logged with their codes.
  - `fileIcon`: accepts only PNG data URLs.
  - `appIcon`: returns `null`.
- Native `ArdiaSystem.*` (in `ac_linux_files.cc`):
  - `openFile`, `moveToTrash` and file drags work only on **completed, non-dangerous** download targets of the window's regular profile. `revealFile` works on any listed target. Anything else fails with `ERR_FILE_NOT_ALLOWED`.
  - Open uses `platform_util::OpenItem` and maps each failure to its own code.
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
  - Native code checks that the path is a completed download and a regular file, replies right away, then runs `Widget::RunDragDropLoop` with `OSExchangeData::SetFilename` in a posted task. That keeps the nested loop out of the IPC handler. `WidgetDeletionObserver` already guards the loop against the window closing.
- **Capabilities:** `ArdiaSystem.fileStatus`, `ArdiaFileDrop.takeDrop` and `ArdiaFileDrag.begin` are advertised in bootstrap capabilities. An older native host therefore gets no drag attempts, no file claims, and `false` from `fileExists`.

## Contract summary

| Call / event | Arguments | Reply |
| --- | --- | --- |
| `downloads.list` | `{}` | `Download[]` (shared `Describe` shape) |
| `downloads.{cancel,pause,resume,keep,remove}` | `{id: GUID}` | `null` |
| `downloads.settings` / `downloads.setSettings` | `{}` / `{folder?, askWhereToSave?}` | `DownloadSettings` |
| event `ArdiaDownloads`/`downloads` `downloads.changed` | `Download` with `profile: ""` | |
| event `ArdiaDownloads`/`downloads` `downloads.removed` | `{id, profile: ""}` | |
| `ArdiaSystem.fileStatus` | `[paths ≤ 64]` | `boolean[]` |
| `ArdiaSystem.openFile` / `revealFile` / `moveToTrash` | `[path]` | `true`, or error `{code, message}` |
| `ArdiaSystem.fileIcon` | `[name, size]` | PNG data URL or `null` |
| `ArdiaFileDrop.takeDrop` | `[names ≤ 64]` | `file:///…` URLs, or `ERR_NO_FILE_DROP` |
| `ArdiaFileDrag.begin` | `[path]` | `true` (drag starts in a posted task) |

## Tests run

- `pnpm --filter @ardia/linux typecheck`: passes.
- `pnpm --filter @ardia/linux test:host`: 221 passed, 0 failed (202 before, plus 19 new in `hostDownloads.test.mjs`, `hostFiles.test.mjs` and `fileDrop.test.mjs`).
- `pnpm --filter @ardia/linux build`: passes. The bundle contains the new drop and drag adapters.
- `python3 -m unittest test_downloads test_contract`: 8 passed. `test_downloads` compiles the helpers and `tests/fixtures/downloads_scenarios.cc` with ASan and UBSan. It covers ID, path and request validation, file URLs, reveal backend choice, Trash (collisions, orphans, symlinks, directories, other filesystems, missing files) and the drop ledger (single use, expiry, name mismatch, bounds). Before the host wiring existed, its five source checks for that wiring failed.
- Full `python3 -m unittest discover -s engine/ardiacore-linux/tests`:
  - Before the change: 119 tests, 1 failure and 4 errors. All five come from the existing close-patch fixtures (`unload_controller` hunks no longer apply to the bundled Chromium 155 tarball after the 156 rebase).
  - After the change: 125 tests with the same five pre-existing failures; all 6 new tests pass.
- `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`: 7 passed.
- The pure helpers pass a syntax-only compile with the Chromium 156 toolchain flags read from `out/Release_GN_linux_x64/obj/ardia/linux/linux.ninja`. `ac_linux_files.cc` and the host hunks could **not** be syntax-checked: that out directory doesn't yet contain the generated mojom headers that `content/` needs. Its Chromium 156 API use was checked by hand against the headers.

## Still needs native runtime acceptance on a Chromium 156 build

I could not build or run Chromium here. These still need checking on a real build:

- The host and `ac_linux_files.cc` compile and link, including `gn check` for the new deps (`//chrome/browser:{browser_process,icon_loader,icon_manager,primitives}`, `//components/download/public/common`, `//ui/display`, `//ui/base/clipboard:file_info`, `//ui/base/dragdrop/mojom`).
- A known download shows progress and completion in Ctrl+J and in the popover, and pause, resume, cancel, keep and discard work.
- After a restart, history entries appear exactly once, cleared ones stay gone, and nothing reports as "live" when it isn't.
- Open, Show in Folder (portal, FileManager1 and `xdg-open` paths) and Move to Trash act on the selected file. Show in Folder reports `ERR_REVEAL_UNAVAILABLE` with no desktop.
- Changing the destination folder and "ask where to save" changes Chromium's behavior. The Settings folder picker relies on `ArdiaShell.pickFolder`, which is still optional and unimplemented on Linux (F05/F26).
- `CanDragEnter` records files from Wayland and X11 file managers, and the DOM `files` names match the recorded basenames. Dropping files, URLs, text and several items at once onto the sidebar and top strip opens tabs. Internal reorder and drops onto web pages are unchanged.
- Dragging a finished download into a scratch application works on Wayland (start_drag serial) and X11. The press does not also open the file. Release before the posted drag starts should simply do nothing.

## Open risks

- `fileExists` answers `false` until the first status reply (about 16 ms plus IPC), so a finished row can briefly render as missing.
- Reveal availability is a heuristic. With a session bus but neither a portal, FileManager1 nor `xdg-open`, Chromium's fallback fails silently.
- Trash only supports the home trash. A download on another filesystem fails explicitly instead of using `$topdir/.Trash-$uid`.
- An inbound claim fails, falling back to DOM URLs and text only, if Chromium's DOM file names ever differ from the recorded path basenames.
