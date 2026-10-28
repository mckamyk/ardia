# #250: Load Unpacked shows Ardia's Add dialog every time

Issue: [#250](https://github.com/mckamyk/ardia/issues/250), found by the desktop-session harness (#240, F09D). Branch `linux-load-unpacked`.

## Cause

Settings › Extensions › Choose Folder… runs `loadUnpacked(SETTINGS_WINDOW_ID)` and, in the same press, `showSettingsSheet(<InstallDialogHost …>)`. The folder picker and the manifest read are asynchronous, so the host mounts before there is an install request. The shared `InstallDialogHost` closes its sheet from an effect whenever it sees no request, including on mount. The sheet therefore closed at once, and the request that arrived after the portal answered had nothing to render it.

That request stayed in `useExtensions().install`. On the next Choose Folder… the host mounted with that stale request already set, so it stayed open and showed the dialog. Cancel cleared it, and the attempt after that failed again. That is the alternation the harness saw: fail, pass, fail, with the miss moving with the portal's answer time. It is a renderer race only. The portal, `extensions.chooseFolder`, `extensions.inspectUnpacked` and the native prompt host answered correctly every time.

## Fix (Linux UI only)

- `apps/linux/src/installRequestSheet.ts`: `followInstallRequest(store, windowId, onClosed)` subscribes to the extensions store. It closes the sheet only when a request for that window goes away after it was there. A request that arrived before the subscription counts as shown.
- `apps/linux/src/SettingsInstallDialog.tsx`: the Linux `InstallDialogHost`. It follows the request with `followInstallRequest`, and renders the shared (lazy) `InstallDialogHost` only while a request for its window exists. The shared host therefore always mounts with a request and never closes the sheet early.
- `apps/linux/vite.config.ts`: an importer-scoped replacement that gives only `components/settings/panes/Extensions.tsx` the Linux host. `vite.shared.ts` exports `lazyComponentId`, because `ExtensionOverlays` resolves to its lazy wrapper id. Browser windows keep the shared `ExtensionOverlays`.

Install consent is unchanged. Nothing installs without Add Extension (`confirmInstall`), and Cancel still runs `cancelInstall`. If the folder chooser is cancelled, the Linux host stays mounted and renders nothing until the next request or sheet.

The shared host has the same mount race on macOS. It is left as is (Linux lane), and the upstream fix would be the same transition rule in `InstallDialogHost`.

## Tests

| Command | Result |
| --- | --- |
| `node … --test src/installRequestSheet.test.mjs` | 5 passed; fails before the fix (module missing) |
| `pnpm --filter @ardia/linux test:graph` | 3 passed. The graph test now requires the Extensions pane to import `SettingsInstallDialog.tsx`, and that file to import the lazy `ExtensionOverlays` and `installRequestSheet.ts`. It failed before the fix. |
| `pnpm --filter @ardia/linux test:host` | 532 passed |
| `pnpm --filter @ardia/linux typecheck` | pass |
| F09D (`acceptance/desktop-session` harness, ee23b331 engine, `--ui-dir` this branch's dist) | 4 of 4 runs pass all three checks. The same engine with a dist built from `linux` fails 2 of 2 runs, as does the packaged ee23b331 UI. |

No native change, so no new engine build is needed. The next package picks it up with the UI bundle.
