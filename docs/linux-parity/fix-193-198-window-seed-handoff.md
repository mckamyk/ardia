# Fix handoff: how a new window's renderer seeds itself (#193, #198)

Branch `t3/fix-window-profiles`, based on `linux` at `1d55d564`. JS only (`apps/linux/src/hostSession.ts`).

## Root cause

Every Linux window has its own renderer, which builds its first session in `initialHostSession`. When the saved session did not hold the window yet, the renderer invented a window and a blank tab for it and saved them like any other window. Two things went wrong with that seed:

- **#193.** It was made in `bootstrap.profileId`, the shell profile (`default`), not the window's page profile. An isolated profile's window became a `default` window in every store once the seed was merged back, so each new tab asked the host for `default` and was refused with `ERR_PROFILE_WINDOW`.
- **#198.** A Little Ardia window is never saved (`snapshotHostSession` drops `kind: "small"`), so its own renderer always seeded itself, as a regular window, and saved it. The opener's next write deleted that record (the opener's snapshot leaves its small window out while its canonical copy had the seed). The deletion came back to the Little Ardia renderer, its store lost its own window, and `windowSync` closed the native window.

## Fix

- The seed uses the window's page profile (`bootstrap.pageProfile`) when it is a regular engine profile, and falls back to `bootstrap.profileId` otherwise. Private windows keep their own seed (`withPrivateWindow`).
- A window that is not the launch window (`bootstrap.startup === false`) and that the saved session doesn't hold belongs to the renderer that opened it. Its own renderer keeps the seed as a local stand-in:
  - `startHostPersistence` leaves the stand-in window, its tabs, and a placeholder profile it had to invent out of every snapshot it writes, so the seed never reaches the session or another renderer, and a placeholder never overwrites the real profile's name.
  - As soon as a merged session holds that window (the opener's save), the stand-in ends and the opener's window, tabs and profile replace it.
  - It reads the session once after subscribing (`session.read`), so an opener's save that landed between the bootstrap and the subscription is not missed.
- A Little Ardia window's stand-in never ends, since nobody saves it: its renderer keeps its window and the opener's writes no longer delete anything.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `host.test.mjs` "an isolated profile's new window seeds itself in that profile before the session that opened it is saved" (from `escalate/isolated-window-seed`) | fail | pass |
| `hostSessionSync.test.mjs` "a window its opener never saves stays out of the session and survives the opener's next write" | fail | pass |
| `hostSessionSync.test.mjs` "an isolated window's stand-in yields to the window its opener saves before its renderer subscribes" / "after its renderer starts" | fail | pass |
| `pnpm --filter @ardia/linux test:host` | | 436 pass |
| `typecheck`, `build`, `test:graph` (3), `acceptance:self-test` (79) | | pass |

The UI built from this branch was run against the existing Chromium 156 build of `linux` `e051e893` (`~/ardia-linux-build-156/artifacts/ardia-linux-x64-e051e893`), read only, with `--results` under `/tmp`:

- W92: 2 of 2 pass. Before, "a Little Ardia window survives a new window's startup sweep" failed.
- F02: "an isolated profile opens its own window and browser context", "isolated profile sees no default cookies or localStorage", "history stays per profile" and "cross-profile tab creation is rejected by the host" pass. Before, the first timed out and the other three could not run. "failed profile creation leaves no phantom window" still fails here; that is #199, a native fix.
- F02-101: 3 of 3 pass. Before, "Find works in an isolated profile's window" timed out.
- S108: "an isolated profile's window blocks the F06 ad image" passes. Its other two checks time out inside the content blocker on this machine (`ac_linux_site_controls_service: the call timed out`, `renderer gone`), which is unrelated to window seeding.

## Limits

- A window whose opener closes before it saves the window is never saved, so it does not come back after a restart. Before, it was saved as a blank window.
- If someone navigates the stand-in's blank tab before the opener's save arrives (300 ms save delay at most), the opener's tab replaces it.
- The Little Ardia renderer still shows a regular window with a blank tab. Its small-window state (kind, size, the claimed page) never reaches it on Linux. That is a separate gap from this issue.
