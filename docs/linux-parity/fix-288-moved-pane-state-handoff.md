# #288: a tab moved to another window takes its blocked pop-ups with it

Issue: [#288](https://github.com/mckamyk/ardia/issues/288), found by the desktop-session harness (#240, wave 2 pages lane, F17D). Branch `fix2-moved-pane-state`.

## Cause

Two halves:

- **Native.** A transfer (`CreateTab` taking a binding from another window) retires the tab's `LinuxPageState` and mounts a new one. The new `LinuxPageStatus` started with an empty `blocked_` map, so its creation snapshot had no `popupBlocked` events and `OpenBlockedPopup` refused the moved tab's ids, although Chromium's `PopupBlockerTabHelper` still held the requests.
- **Shell.** On macOS one JS runtime serves every window, so page state follows the tab. On Linux each window has its own renderer and `usePages` store; the old window kept the moved tab's status and pop-up list after its view unmounted.

## Fix

- `LinuxPageStatus` adopts `PopupBlockerTabHelper::GetBlockedPopupRequests()` when it is created, recording each for the current document as `BlockedUrlAdded` does (same 100-entry cap, same origin). The helper clears its list on a new document, so nothing stale is adopted. The new window's snapshot lists the pop-ups and Only Once / Always Allow work there.
- When a host tab view unmounts and its tab no longer belongs to this window (moved or closed), `forgetDepartedPage` drops its page state, popover and readiness from this renderer's store. A tab that only remounts in the same window keeps them. Hover status is live state: the new window shows it again on the next hover.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_page_state` | 30 passed; new `remounted-popups` fails before (the remounted page lists no blocked pop-up) |
| `node --test src/hostPageStatus.test.mjs` | 6 passed; the new case fails with `forgetDepartedPage` stubbed |
| `bash scripts/linux/syntax-check-156.sh ac_linux_page_status` | pass |
| `pnpm --filter @ardia/linux typecheck`, `test:host`, `build` | pass |

## Needs the next build to confirm

F17D's "a pane moved to a new window takes its status and blocked pop-up with it and the old window shows none" needs the native half, so a package built from this branch.

## On the 148104a3 build

The pop-up now reaches the new window and the old window keeps nothing. F17D still failed at the next step, the hover status in the new window. sway tiles the new window to 960 px, so pane B is 763 px wide, and the fixture's link sits at x 600–1020: F17D pointed at its centre (x 810, past the pane's edge, off the 1920 px screen). Chrome never sent a target URL because the pointer never reached the link. F17D now hovers 40 px into the link. With that, F17D passes 3 of 3 on the 148104a3 package with its own UI.
