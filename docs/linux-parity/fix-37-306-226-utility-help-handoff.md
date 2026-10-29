# Fix handoff: help commands from Settings, the upscaling row and the quit wording (#37 F27D, #306, #226)

JS only, in `apps/linux`. macOS, the shared browser and the demo are unchanged.

## Help commands pressed in Settings or Import (#37, F27D)

macOS has one JS runtime: `startOnboarding("settings")` resolves the id to the frontmost browser window (`resolveWindowId`). On Linux each window has its own renderer, so a command the host sends with `windowId: "settings"` reached only the Settings renderer, whose `startAppCommands` handled only `UTILITY_APP_COMMANDS` and dropped the rest. On top of that, `startLinuxShortcuts` bound only Close, Settings, Find and Quit in utility windows, so a Show Onboarding shortcut (F27D records Ctrl+Alt+O) never fired there.

- `browserCommandRoute.ts`: `BROWSER_WINDOW_COMMANDS` (Show Onboarding, Tool Tour, Video Tour, Release Notes, Send Feedback) are handed over a `BroadcastChannel` (`ardia-browser-commands`).
  - **Target.** The utility posts `find`. Every serving browser renderer answers with its id and its `regularFocusOrder`: the store's `ui.focusOrder`, then `windowOrder`, kept to regular, non-Little-Ardia windows, as `hostWindowProfiles.ts` does. Browser renderers follow every window's activation through the host's `focus` window event, so a click into a page counts. The utility's own `ui.focusOrder` can't be used: the host broadcasts window events to browser windows only, and the session sync carries no focus. After 150 ms the utility picks the first window in that order that answered, and sends it `run`.
  - **One answer.** The chosen window answers `claim` and runs only on the utility's `go`. The utility sends `go` and reports success only if the claim arrives within 1.5 s. Otherwise it shows "No browser window to show this in" and never sends `go`, and the window drops its claim after 1.5 s. So a late window never runs a command Settings has already reported as failed. On `go` the window focuses itself (`ArdiaShell.focusWindow`) and runs the command through `runAppCommand`.
  - **Private windows never serve.** Every window's shell document lives in the regular app profile (`chrome://ardia` refuses OTR profiles), so private shells share the channel; `startLinuxAppServices` therefore skips `serveUtilityCommands` when `bootstrap.privateWindow` is set, as `hostPolicies.ts` does for the tab-policy channel. With only private windows open, Settings says there's no browser window.
- `startAppCommands` routes those commands from utility windows; `startLinuxAppServices` serves them in regular browser windows.
- `shortcuts.ts` `utilityBindings`/`UTILITY_SHORTCUT_COMMANDS`: utility windows keep the help shortcuts and the utility app commands (diagnostics, performance recording, Task Manager) besides Close, Settings, Find and Quit.

Tests: `browserCommandRoute.test.mjs` (7) with real Node `BroadcastChannel`s:
- the first window of the focus order runs the command once and is focused, and a changed order changes the target;
- a focused window that doesn't serve is skipped;
- the order drops private and Little Ardia windows;
- no window answers null;
- non-help commands are refused;
- a late claim never gets `go`, and an unanswered claim runs nothing.

`appServices.test.mjs`: Show Onboarding from Settings plays in the most recently focused of two windows, which is focused; private windows don't serve; with no browser window the toast shows. `shortcuts.test.mjs`: Ctrl+Alt+O for Show Onboarding stays bound in utility windows. Harness F27D on the 148104a3 engine with this UI: 7 of 7 pass (it failed "nothing played after Ctrl+Alt+O in Settings" before).

## Video upscaling row (#306)

Upscaling is macOS Metal. `LinuxCapabilities.videoUpscaling` is false and `unavailableSettingIds` hides `tabs.videoUpscaling`, so the row, its search entry and its anchor go away like the other unavailable rows.

## Warn before quitting wording (#226)

The row read "Asks before Ardia quits with Ctrl+Q." (the shortcut transform's rendering of ⌘Q); Linux quits with Ctrl+Shift+Q. `linuxWording.ts` now says so. Wording replacements also match the Linux shortcut rendering of their source text, because the component shortcut transform runs before the wording transform in the Vite build.

Left on #226: closing the last window with a running download still shows Chromium's `DownloadInProgressDialogView`. Whether to keep it, route it through F05 or keep the app running with no windows is a product decision.
