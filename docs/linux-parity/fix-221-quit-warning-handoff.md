# Fix handoff: Warn before quitting and the active-download question on quit (#221)

Branch `t3/linux-quit-warning`, based on `linux` at `8f22774f`. JS in `apps/linux` (new `quit.ts`, the shortcut catalogue and the app-command router), native in `ac_linux_host.cc` (two routes, one registration call), `ac_linux_close_hooks.{h,cc}` (one flag), `ac_linux_dialog_views.cc` (a background log line) and one hunk of `chromium-linux-document-close.patch`. macOS, the shared browser and the demo are unchanged.

## What macOS does

`packages/shell/ios/WindowManager+Quit.swift`:

- `confirmQuit` (the Quit menu item, ⌘Q): when `warnBeforeQuitting` is on, one app-modal alert, "Are you sure you want to quit Ardia?", "Your windows and tabs will be restored the next time you open it.", Quit and Cancel, and a "Don't ask again" box. A ticked box emits `quitWarningSuppressed` whatever the answer, and `nativeSync.ts` turns the setting off. Cancel ends the quit.
- `shouldTerminate`: before saving, `confirmActiveDownloads` asks "You have N download(s) in progress. If you quit now, this download will be / these downloads will be cancelled." with Quit and Cancel.
- Closing the last window doesn't quit the app on macOS, so it never shows the quit warning.

## What Linux had

There was no quit command at all: `std:` items were filtered out of the Linux shortcut catalogue and Linux has no app menu. A Chromium quit (SIGINT, SIGHUP, the last window) went through `BrowserCloseManager`, which showed Chromium's own `DownloadInProgressDialogView` for active downloads: not an owned F05 dialog, and visible even in background instances. #212 already sends `quitting` on `shell.closeRequested`, so a quit skips each window's "Close N tabs?" warning.

## The fix

- **Command.** `std:quit:` stays in the Linux catalogue (`LINUX_STANDARD_ITEMS` in `commandCatalog.ts`) with the Linux default Ctrl+Shift+Q (`⇧⌘Q`, ⌘ being Ctrl on Linux). It shows in Settings › Shortcuts as "Quit Ardia" under Ardia and can be remapped. The id matches macOS's, so a synced remap carries over. The binding dispatches the command `quit` with no argument. Utility windows (Settings, Import) keep the binding and handle it too.
- **Prompts (`quit.ts`).** `createLinuxQuit` runs one attempt at a time:
  1. if `warnBeforeQuitting`, ask the quit warning through the F05 owned dialog (`confirm` → `linuxHostDialogs()`); a ticked "Don't ask again" turns the setting off on Quit and on Cancel, Cancel ends the attempt;
  2. ask the host for `ArdiaShell.downloadsInProgress` (Chromium's `DownloadCoreService::BlockingShutdownCountAllProfiles`, every profile including private ones); if any are running, ask the downloads question with macOS's words; Cancel ends the attempt;
  3. call `ArdiaShell.quit`.

  Pressing the key again while a prompt is open returns the same attempt. Across windows, the attempt holds the Web Lock `ardia.linux.quit` with `ifAvailable`, so a second window's Ctrl+Shift+Q while the first window asks does nothing (`busy`). A host without the two routes rejects with `ERR_UNAVAILABLE` before asking anything, and the command shows "Not available on Linux yet". An unreadable count fails closed (`ERR_QUIT`) with a "Ardia couldn't quit" toast.
- **Native quit (`ArdiaShell.quit`).** It takes no arguments, replies, then posts `chrome::AttemptExit()`. `AttemptExit` sets `browser_shutdown::SetTryingToQuit(true)`, which #212's `quitting` flag reads, so no per-window prompt appears. `AttemptUserExit` would not set it on Linux. A beforeunload veto goes through `BrowserCloseManager::CancelBrowserClose`, which clears it again.
- **One download question.** `StartLinuxHost` calls `SetLinuxQuitAsksAboutDownloads(true)`, and the document-close patch's `BrowserCloseManager::CheckForDownloadsInProgress` goes straight to `CloseBrowsers()` when that flag is set. Ardia asked already for a user quit, and signal quits (SIGINT/SIGHUP through `AttemptExit`) must not ask. SIGTERM and session end go through `chrome::SessionEnding()`, which never reached this check, so #212's no-prompt, still-saving behaviour is unchanged.
- **Background instances.** F05 already answers every dialog in a background instance with a forced Cancel and never shows it. `Present` in `ac_linux_dialog_views.cc` now also logs it, in the shape the file dialogs use: `[ardiacore] dialog (background run, not shown): "<title>" (<confirm> / <cancel>): <cancel>`. A background Ctrl+Shift+Q with the warning on therefore logs one line and keeps running.

## Tests

Each was written first and failed before the change.

- `apps/linux/src/quit.test.mjs` (9, in `test:host`) runs the real F05 adapter against a fake host: wording and buttons, Cancel keeps everything, Quit quits once, "Don't ask again" on Quit and on Cancel, setting off, the downloads question (singular and plural, Cancel and Quit), a repeated key press while a prompt is open, a busy lock in another window, a missing route and an unreadable count.
- `shortcuts.test.mjs`: Ctrl+Shift+Q dispatches `quit` with no argument, the Settings entry and its remap, and only one `std:` entry.
- `appServices.test.mjs`: utility windows include `quit`, and without a host the command says it's unavailable.
- `engine/ardiacore-linux/tests/test_quit.py` (3) compiles the production `ArdiaShell.quit`/`downloadsInProgress` branch: count reply, argument refusal, quit posted rather than run inside the reply, advertised capabilities, the host registering the flag, and the patch hunk.
- `test_chromium_close.py` `ardia-asked-downloads` runs the patched `BrowserCloseManager` with 2 pending downloads and the flag set: no Chromium download prompt, and the quit completes. The existing `downloads` scenario still covers the unset flag. Reverting the hunk fails it.
- `test_dialogs.py` checks the background log line from the Views presenter fixture.
- Acceptance `Q221` (`scenarios/q221-quit-warning.mjs`) has automated checks: the routes and a zero count; with the warning on, a background quit logs one prompt and keeps running; SIGTERM quits without a dialog and both windows restore; with the warning off, the quit command quits the app and both windows restore. Its two manual steps cover the visible prompt and the downloads question.

`scripts/linux/syntax-check-156.sh` passes for `ac_linux_host.cc`, `ac_linux_close_hooks.cc` and `ac_linux_dialog_views.cc`. Its overlay reports a conflict for `browser_close_manager.cc` because the 156 tree carries the older version of that hunk. That file isn't one the script compiles; `test_chromium_close.py` applies the whole patch at zero fuzz to the pinned 156 sources and compiles the patched methods.

## Not covered

- No Chromium build or packaged engine with this change was run. `Q221` and the manual steps need the next Linux engine package.
- Closing the last window while a download runs still goes through Chromium's `Browser::OkToCloseWithInProgressDownloads` (`DownloadCloseType::kLastWindow`) and its own dialog. Closing the last window isn't a quit on macOS, so that question has no macOS counterpart; it is left as it was.
- Settings › General still describes the row as "Asks before Ardia quits with ⌘Q.", the shared wording.
