# Fix handoff: help commands from Settings, the upscaling row and the quit wording (#37 F27D, #306, #226)

JS only, in `apps/linux`. macOS, the shared browser and the demo are unchanged.

## Help commands pressed in Settings or Import (#37, F27D)

macOS has one JS runtime: `startOnboarding("settings")` resolves the id to the frontmost browser window (`resolveWindowId`). On Linux each window has its own renderer, so a command the host sends with `windowId: "settings"` reached only the Settings renderer, whose `startAppCommands` handled only `UTILITY_APP_COMMANDS` and dropped the rest. On top of that, `startLinuxShortcuts` bound only Close, Settings, Find and Quit in utility windows, so a Show Onboarding shortcut (F27D records Ctrl+Alt+O) never fired there.

- `browserCommandRoute.ts`: `BROWSER_WINDOW_COMMANDS` (Show Onboarding, Tool Tour, Video Tour, Release Notes, Send Feedback) are handed over a `BroadcastChannel` (`ardia-browser-commands`, same origin and storage partition as the regular profile's renderers). The utility posts `find`; every browser renderer answers with the time it last took focus; after 150 ms the utility sends `run` to the most recent one, which acknowledges, focuses its window (`ArdiaShell.focusWindow`) and runs the command through `runAppCommand`. No answer, or no acknowledgement within 1.5 s, shows "No browser window to show this in" in the utility window instead of doing nothing.
- `startAppCommands` routes those commands from utility windows; `startLinuxAppServices` serves them in browser windows.
- `shortcuts.ts` `utilityBindings`/`UTILITY_SHORTCUT_COMMANDS`: utility windows keep the help shortcuts and the utility app commands (diagnostics, performance recording, Task Manager) besides Close, Settings, Find and Quit.
- Private windows use another storage partition, so they don't answer; with only private windows open the utility says there's no browser window.

Tests: `browserCommandRoute.test.mjs` (3) with real Node `BroadcastChannel`s: the most recently focused of three windows runs the command once and is focused, no window answers null, non-help commands are refused. `appServices.test.mjs`: Show Onboarding from Settings plays in the browser window and is focused; with no browser window the toast shows. `shortcuts.test.mjs`: Ctrl+Alt+O for Show Onboarding stays bound in utility windows. Harness F27D on the 148104a3 engine with this UI: 7 of 7 pass (it failed "nothing played after Ctrl+Alt+O in Settings" before).

## Video upscaling row (#306)

Upscaling is macOS Metal. `LinuxCapabilities.videoUpscaling` is false and `unavailableSettingIds` hides `tabs.videoUpscaling`, so the row, its search entry and its anchor go away like the other unavailable rows.

## Warn before quitting wording (#226)

The row read "Asks before Ardia quits with Ctrl+Q." (the shortcut transform's rendering of ⌘Q); Linux quits with Ctrl+Shift+Q. `linuxWording.ts` now says so. Wording replacements also match the Linux shortcut rendering of their source text, because the component shortcut transform runs before the wording transform in the Vite build.

Left on #226: closing the last window with a running download still shows Chromium's `DownloadInProgressDialogView`. Whether to keep it, route it through F05 or keep the app running with no windows is a product decision.
