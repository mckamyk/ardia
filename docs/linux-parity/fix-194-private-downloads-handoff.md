# Fix handoff: private downloads in background instances (#194)

Branch `t3/fix-window-profiles-194`, based on `linux` at `1d55d564`. Native (`ac_linux_background_dialogs.{h,cc}`, one line in `StartLinuxHost`) and the F02-72 scenario.

## Root cause

Neither suspect in the issue was it. The off-the-record profile does get a delegate, and its download prefs carry the regular profile's folder. The cause is Chrome's own incognito behaviour. On desktop, Chrome 156's `DownloadBubbleUIControllerDelegate` (`chrome/browser/download/download_ui_controller.cc`) sets `prefs::kPromptForDownload` to true on every off-the-record profile, so incognito asks where to save every download. The same code runs on macOS.

A background instance can't show that dialog. On Linux, with headless Ozone, Chrome's own dialog can't open at all, so the request comes back cancelled. The download then ends as `USER_CANCELED` with no target path.

I measured this on the existing `e051e893` build with a throwaway probe scenario (not committed), reading Chrome's histograms over CDP (`Browser.getHistograms`):

| Histogram | Value |
| --- | --- |
| `Download.CancelReason` | 1, `kTargetConfirmationResult` |
| `Download.PathGenerationEvent.UserDownload` | 2, `USE_LAST_PROMPT_DIRECTORY`: a confirmation reason was set right after the filename was generated |
| `Download.PathValidationResult.UserDownload` | 0, success: the folder was fine |

Other probe results:
- The same happens in a non-background instance under headless Ozone, and after a regular download has initialised download history.
- It also happens with no download preferences seeded at all.
- A regular download with Ask where to save turned on cancels the same way.
- Wrapping `chrome.send` in the private renderer showed that the UI sends no `downloads.cancel`.

So in a visible instance a private download should open Chrome's save dialog, as Chrome's incognito and Ardia on macOS do. Nobody has checked that by hand on Linux yet; it's the last step below. What was missing is a way for a background test instance to answer that dialog. macOS has one: `ArdiaActivation.mm` logs each file panel and answers it from `file-chooser.txt` in the data dir, or cancels.

## Fix

- `ac_linux_background_dialogs.{h,cc}`: in background instances only, `StartLinuxHost` installs a `ui::SelectFileDialogFactory`. Each dialog then:
  - logs its kind, title and default path (`[ardiacore] save file dialog (background run, not shown): …`);
  - reads `<user data dir>/file-chooser.txt` on a blocking task and deletes it, so one file answers one dialog;
  - answers with the first absolute path in it (every path for a multi-file open), or cancels when there is none.
  - Lines that aren't absolute paths are skipped. The file is capped at 64 KiB and 64 paths.
- Visible instances are unchanged and keep Chrome's real dialog. Private downloads still ask where to save, as Chrome's incognito does, and nothing about them reaches the regular profile.
- F02-72 writes the answer before each private download and checks that the download asked. The scenario row in `acceptance-suite.md` says so.

Other background file dialogs (Save Page, file inputs) now get this logged cancel too, unless a test supplies an answer. The folder picker (`ArdiaShell.pickFolder`) already answers `null` in background instances before it makes a dialog.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `test_background_dialogs.py` (4: answer parsing compiled and run, background-only install from `StartLinuxHost`, one-shot read off the UI thread with cancel when empty, BUILD.gn and no comments) | fail (no helper) | pass |
| `scripts/linux/syntax-check-156.sh ac_linux_background_dialogs ac_linux_host.cc` | | ok |
| `pnpm --filter @ardia/linux acceptance:self-test` | | 79 pass |

## Needs a Chromium 156 build

This was not compiled into a build or run. After the next incremental build, run `--only F02-72`. The private download should finish into the scratch folder, and the log should show the save dialog it answered. By hand on a desktop: download something in a private window, and Chrome's save dialog should open over that window.
