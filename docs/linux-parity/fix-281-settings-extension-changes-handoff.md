# #281: Settings › Extensions follows installs while it is open

Issue: [#281](https://github.com/mckamyk/ardia/issues/281), found by the desktop-session harness (#240, F02D). Branch `fix/281-settings-extension-changes`.

## Cause

There were two gaps, one in the UI and one in the host:

- **UI.** Linux Settings is a utility window with its own document. Only browser windows start the shared extensions bridge (`startNativeSync` → `startExtensionsBridge`), and that bridge is the only `extensions.changed` listener. The Settings window loads a profile's list once (`useResource(extensionList, profile)`) and never refreshes it. This was true for the default profile too: with Settings › Extensions open, an install made from a browser window didn't show until Settings reopened. (The issue's "the default profile's pane does pick up changes" came from installs made in the pane itself, which refresh directly.)
- **Host.** `extensions.broadcast` sends a change only to hosts whose page profile is that profile (or its private profile). The Settings host runs in the default profile, so it never hears about an isolated profile's changes.

## Fix

- **UI** (`apps/linux/src/utilityExtensions.ts`, wired in `main.tsx`'s utility branch): `followExtensionChanges` listens for `extensions.changed` in utility windows. For a profile whose list the window already holds, it runs `refreshExtensionsAfterChange`. A profile it never showed isn't fetched.
- **Host** (`ac_linux_host.cc`, `extensions.broadcast`): utility hosts also get changes for every regular profile, but never an off-the-record profile's. Browser windows keep their per-profile rule. Settings can already list any profile, so it learns nothing new.

## Tests

| Command | Result |
| --- | --- |
| `node … --test src/utilityExtensions.test.mjs` | 2 passed. It refreshes each shown profile, and ignores unshown profiles, other events, other modules and malformed payloads. Fails before the fix (module missing). |
| `python3 -m unittest test_extensions.ExtensionBroadcastTests` | passes. The compiled broadcast lambda now also has to reach a Settings host for the isolated and default profiles, never for a private profile, and nothing once its profile shuts down. Fails before the host change. |
| `pnpm --filter @ardia/linux test:host` / `test:graph` / `typecheck` | 535 / 3 passed / pass |
| `scripts/linux/syntax-check-156.sh ac_linux_host.cc` | pass |
| Desktop session, ee23b331 engine, Settings › Extensions (default profile) open while a browser window installs the fixture | passes with this UI; times out with the `linux` UI |
| F02D's isolated-profile check, ee23b331 engine with this UI | still fails: that engine doesn't send the isolated profile's change to Settings. It needs the next engine build. |

`test_extensions.ExtensionWiringTests.test_action_popup_hook_patch_is_registered_for_linux_and_applies` fails both here and on `linux` (a patch dry run against the 156 tree), so it predates this change.
