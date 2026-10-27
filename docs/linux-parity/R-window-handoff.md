# Lane R handoff (1 of 2): window, session and profile follow-ups

Branch `t3/linux-window-followups`, based on `linux`. Source-level fixes for [#92](https://github.com/mckamyk/ardia/issues/92), [#77](https://github.com/mckamyk/ardia/issues/77), [#79](https://github.com/mckamyk/ardia/issues/79), [#70](https://github.com/mckamyk/ardia/issues/70) and [#114](https://github.com/mckamyk/ardia/issues/114), tracked by [#140](https://github.com/mckamyk/ardia/issues/140). The downloads, DevTools, extension-prompt and tooling half is in [R-downloads-handoff.md](R-downloads-handoff.md).

None of this has been compiled into Chromium 156 or run natively. The native hunks were checked with `clang -fsyntax-only` against the 156 tree's headers and its own `linux.ninja` flags (read only, nothing written into that tree). They add no errors; the host already has eight errors on `linux` that are tracked in [#157](https://github.com/mckamyk/ardia/issues/157).

## #92: startup sweep closing live windows

Every Linux window has its own renderer, and each one runs the shared `windowSync` sweep at startup, which closes native windows missing from that renderer's store. A bootstrap session can legitimately predate a live window: Little Ardia windows are never saved, and a window opened inside the 300 ms save delay isn't in the snapshot yet.

**Contract.** On Linux a window's own renderer and its close path own its lifetime. The Linux `windowIds` adapter (`apps/linux/src/windowSweep.ts`) reports only the native windows this renderer's store knows, so the sweep retitles those and closes nothing it can't rule on. Native `ArdiaShell.windowIds` is unchanged. The shared `windowSync` is unchanged.

As a consequence, orphan-window cleanup on Linux now relies on each window's own close path (its renderer, the native close interception and `OnBrowserClosed`); no renderer's startup sweep removes a native window another renderer still shows.

## #77: fullscreen restore at launch

`ArdiaShell.restoreFullScreen(ids, focus)` is implemented natively with a `FullScreenRestore` ledger (`ac_linux_fullscreen_restore.{h,cc}`, standard library only).

- Only the launch window's renderer (`startup_`, not a utility) is heeded, and only its first request per process. Later renderers send the same call from `nativeSync` and get a no-op success.
- Ids must be 1–128 characters with no `/`, `\` or NUL; at most 64; duplicates collapse. Malformed arguments are rejected.
- A listed window enters browser fullscreen (`FullscreenController::ToggleBrowserFullscreenMode`) once it is attached, visible and not minimized: at the call, at `Initialize` and at `RendererReady`. A window already in browser fullscreen isn't toggled.
- Entering is observed through `OnWidgetShowStateChanged` with `WindowFullScreen()` (window fullscreen, not page fullscreen, from #130).
- After 10 s anything that hasn't arrived or entered is given up and a `fullScreen` off event is broadcast, so stores drop the stamp (as macOS does). A window closed meanwhile is dropped at once.
- When everything settled and something was toggled, the saved focused window is activated through the focus ledger, only if an Ardia window is active, only if no pointer press or activation change happened since the restore began (the ledger's epoch), and never in background instances. The saved `focusedWindowId` is always the launch window on Linux (`hostSession.ts` sets it at bootstrap), so in practice this returns focus to the launch window.
- A window is also tried when it becomes visible (`OnWidgetVisibilityChanged`), not only at attach and readiness.
- `restoreFullScreen` is an optional shell method; the Linux module resolver exposes optional methods only when the host advertises them, and the real shell module called `undefined` before. The bootstrap now advertises it, and `shellCapabilities.test.mjs` fails for any optional method with a native branch that isn't advertised.

Windows open with their saved frame first, so on X11 they go fullscreen on the display that frame is on. Wayland gives no positions, so the compositor picks the output there.

## #79: keys typed before a new window's UI is ready

From `Initialize` until its UI reports ready, a regular window's root key handler holds every key press and release it owns (`OwnsKeyboardEvent`: not other widgets, not under a modal dialog) in a bounded `Typeahead` buffer (`ac_linux_typeahead.h`, 512 events) and stops them before shortcut handling. Stopping the post-IME dispatch also keeps Chrome's input method from inserting the character. Past 512 events the whole buffer is discarded and further keys are consumed until the UI is ready, so nothing is replayed truncated or out of order.

`ArdiaShell.typeaheadReady(field: boolean)` (advertised capability; utility windows get an error) posts a replay. Held events are replayed, in order, through the window's input method (`InputMethod::DispatchKeyEvent`) only when `field` is true and the shell WebView itself has focus with a text input client (`ShellHoldsTextField`, rechecked before each event; the replay stops when focus leaves). Otherwise, and when the 10 s deadline passes first, the held keys are discarded: they never reach a page that focused a field of its own.

The Linux UI (`typeahead.ts`) announces readiness after the first window commit: `true` as soon as one of its text fields holds focus, `false` after 500 ms without one.

Left: text an IME commits asynchronously during the load, and replayed keys passing through the input method a second time ([#154](https://github.com/mckamyk/ardia/issues/154)).

## #70: isolated profiles and paging

**Decision.** Linux keeps one engine profile per regular window. macOS pages isolated profiles inside one window by keeping a Chrome Browser per profile per window; the Linux host attaches one Browser to each window, and the equivalent needs hidden companion Browsers with their own tab strips, viewports and lifetimes ([#153](https://github.com/mckamyk/ardia/issues/153)). Shared-data profiles switch in place; an isolated profile lives in its own window, which switching focuses or opens.

**What changed.** The transient wrong-window state is gone. The Linux pager shim asks a reach callback whether the window hosts a page (`setPagerReach`; installed by `installProfileWindows` with the same `pagesInPlace` rule its `switchProfile` routing uses):

- a drag stops at a profile the window can't host (rubber band at that edge) and never selects it; a release that would have reached it springs back and routes to that profile's window;
- a programmatic switch or step to such a profile is refused, so the shared `ProfilePager` falls back to `switchProfile`, which routes.

## #114: site zoom for any named profile

`sites.zoomLevels` and `sites.setZoom` validate their arguments, then route by the named profile:

- a private name follows #161: it is accepted only from the private window that hosts it (`RoutePrivateCall`), and its own zoom map is read or written through the original profile's engine call with the off-the-record flag;
- every other name resolves through the profile registry like the other `sites.*` calls: `""`/`default` is the app profile, an isolated id its own profile, and an off-the-record result is refused with `ERR_PROFILE_PRIVATE`.

The calling window's page profile no longer matters for regular profiles, so Settings lists and resets any regular profile's zoom levels; private zoom levels are never listed or written from another window. This matches macOS.

## Tests

| Command | Result |
| --- | --- |
| `pnpm --filter @ardia/linux typecheck` | passes |
| `pnpm --filter @ardia/linux test:host` | all pass (counts in the PR body) |
| `pnpm --filter @ardia/linux test:graph` | 3 pass |
| `pnpm --filter @ardia/linux build` | passes |
| `pnpm --filter @ardia/linux acceptance:self-test` | all pass |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | all pass (counts in the PR body) |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 pass |

New or extended: `windowSweep.test.mjs` (3), `typeahead.test.mjs` (4), `profilePaging.test.mjs` (5), `shellCapabilities.test.mjs` (2), `test_fullscreen_restore.py` (6, compiled ledger + host glue), `test_typeahead.py` (6), three new compiled scenarios in `test_window_lifecycle.py`'s real-host harness, and `test_profile_host.py`'s zoom test rewritten for the named-profile contract. Each was failing before its fix.

## Acceptance scenarios

`apps/linux/scripts/acceptance/scenarios/r-window-followups.mjs`: W92 (reload and new-window sweeps keep live and Little Ardia windows), W77 (seeded fullscreen window restores, its sibling stays windowed, a later renderer doesn't restore again), W79 (capability and readiness; the typing itself is manual), W70 (shared profile in place, isolated profile in its own window, no duplicate window), W114 (Settings sets, lists and resets an isolated profile's zoom; private refused).

## Needs native runtime acceptance (not done here)

I could not build or run Chromium 156. On a build:

1. W92, W77, W79, W70 and W114 in the acceptance suite.
2. Ctrl+N then immediate typing on Hyprland and X11, and typing right at launch (W79 manual).
3. Fullscreen restore on a second monitor (W77 manual).
4. Trackpad swipes toward an isolated profile stop at the edge and bring that profile's window forward (W70 manual).
