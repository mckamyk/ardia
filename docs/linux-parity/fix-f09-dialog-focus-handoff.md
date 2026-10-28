# F09C and F09S on 148104a3: the install dialog's focus and dismissal

Coordinator follow-up for [#19](https://github.com/mckamyk/ardia/issues/19). Branch `fix/f09-focus-dialog`.

## F09C: no keyboard focus after clicking the prompt (product)

The failing capture has the CRX prompt in the shell's DOM but nothing of it on screen. The page, still white and loading, covers the whole page area. A sidebar hover card is up, and its cut-out shows the shell's own background rather than a captured backdrop. The click on the prompt's heading therefore reached the page, and the shell never got document focus.

A hover card over the page puts `HostWebView` on its occlusion path, which asks the host for `captureViewport` to paint a backdrop behind the cut-out. That command is `CopyFromSurface`, and on a page that hasn't painted it can wait for a frame. It went through the tab's ordered command queue, so the `tab.viewport` update that hides the page under a dialog waited behind it. This is the same failure as #278's `webStoreRequest`.

`captureViewport` now joins `downloadFavicon` and `webStoreRequest` in `UNORDERED_COMMANDS` (`apps/linux/src/hostTab.ts`). `HostWebView` already awaits the capture before sending the occluded viewport it paints, so the order that matters is kept by the caller.

The failure is intermittent. 1 of 13 runs failed on the packaged 148104a3 UI, and 0 of 4 failed with this UI. The new unit test is the deterministic check.

## F09S: the dialog closed on the harness's focus click (harness)

On macOS too, Ardia's install dialog closes when you click outside it: the shared `Dialog` in `ExtensionOverlays.tsx` puts a scrim under it whose `onPress` is `cancelInstall`. That counts as Cancel, so nothing installs. Linux matches, so the product is right. The scenario focused the window with `clickNeutral`, a click on the sidebar, which lands on the scrim and cancels before Ctrl+W.

F09S now clicks the dialog's own heading to focus the window, as F09C does, and waits for document focus before Ctrl+W.

## Tests

| Command | Result |
| --- | --- |
| `node … --test src/hostPageCommands.test.mjs` | new test: with a capture pending, a hiding `tab.viewport` still goes out. Fails before the fix ("hiding the page waited behind the capture"). |
| `pnpm --filter @ardia/linux test:host` / `acceptance:self-test` / `typecheck` | 593 / 126 passed / pass |
| F09C, 148104a3 engine, this UI | 4 of 4 runs pass all four checks |
| F09S, 148104a3 engine, this UI, network | new scenario 3 of 3 pass; the old scenario fails "the dialog closed when the window was clicked, before Ctrl+W" |

UI and harness only. No engine build is needed.
