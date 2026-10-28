# #272: a Load Unpacked request left behind by another sheet never shows first

Issue: [#272](https://github.com/mckamyk/ardia/issues/272), from the review of #255. Branch `fix/272-leftover-install-request`.

## Cause

The Linux `InstallDialogHost` (#250) counted a request already in the store when it subscribed as one it had shown, and rendered it. If another Settings sheet replaced the holder while the folder chooser was open, that folder's request landed with nothing to show it and stayed in `useExtensions().install`. The next Choose Folder… mounted a new holder, which showed the old request at once, before the new chooser returned. Add Extension or Enter would then install the previous folder.

## Fix (Linux UI only)

- `apps/linux/src/installRequestSheet.ts`: `ownsInstallRequest(install, windowId, leftover)` is true only for a request of this window that isn't the left-over one. `followInstallRequest` takes the left-over request and neither follows it nor closes the sheet when it goes away.
- `apps/linux/src/SettingsInstallDialog.tsx`: the holder captures the store's request once, when it mounts (a `useState` initializer, so a re-render doesn't change it). That request predates the press, so the holder never renders it. A request that arrives after the mount, including one between mount and subscription, is the press's own and shows as before.

The left-over request is ignored, not cancelled, so nothing answers a request on a re-render. The new folder's request replaces it as before.

In a desktop session, the Settings window is modal while the portal folder chooser is open, so a person can't open another sheet until the chooser answers. The end-to-end check below plants the left-over request directly.

## Tests

| Command | Result |
| --- | --- |
| `node … --test src/installRequestSheet.test.mjs` | 8 passed. New: a left-over request is neither shown nor followed, and the next folder's request is; the left-over going away doesn't close the sheet or answer it; only the own window's request is owned. Fails before the fix (no `ownsInstallRequest`). |
| `pnpm --filter @ardia/linux test:host` / `test:graph` / `typecheck` | 537 / 3 passed / pass |
| Desktop session, ee23b331 engine, portal mock: plant an unanswered "Previous Folder" request for Settings, then press Choose Folder… with a 4 s portal answer for "Chosen Folder" | this UI: only "Add “Chosen Folder”?" appears, after the answer, and Cancel installs nothing. `linux` UI: "Add “Previous Folder”?" shows 1.5 s after the press, before the chooser returns. |
| F09D, ee23b331 engine, this UI | 3 of 3 pass (no #250 regression) |
