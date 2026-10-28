# Linux media and tab-policy follow-ups (lane T, part 2)

Branch `t3/linux-media-followups`. Source-level work for #123, #125, #126 and #127. Tracked by #165. No Chromium 156 GN, compile, link or runtime run was possible in this lane: everything below is source-level and fixture-level evidence. The page messaging and page-status half, including the native `engine.inputActivity` event, is on `t3/linux-page-followups` (`docs/linux-parity/t-page-followups-handoff.md`).

## What changed

### One tab-policy owner per app (#126)

- Every browser window still runs the shared tab lifecycle for its own tabs (sleep sweeps, freezes, recent-tab warm-up).
- The app-wide parts now have one owner: the window holding the `ardia.linux.tab-policies` Web Lock, the same mechanism as the app-launch lock. When the owner closes, the lock passes to a waiting window. Without Web Locks a window runs everything itself.
- Only the owner runs the memory trim, which trims the whole engine.
- `tabLifecycle.ts` gets two importer-scoped Vite replacements (only for that importer):
  - `policyPerformance.ts`: the owner applies `engine.batterySaver` and shares its reply on the `ardia.linux.tab-policies` BroadcastChannel. Other windows reuse a reply for the same mode, or wait up to 2 s and then apply it themselves.
  - `policyToasts.ts`: Battery Saver toasts appear only in the renderer that owns the focused window. The host broadcasts window activation to every renderer, so all renderers agree on `ui.focusedWindowId`.
- Private windows never request the lock, never run the trim and ignore the channel; they apply Battery Saver themselves (idempotent), so a private shell in its own storage partition can't become a second owner.
- Each Battery Saver request takes a generation. A request that timed out waiting for the owner applies only if it is still the latest; otherwise it resolves with the latest request, so a stale fallback can't overwrite a newer mode.
- The `stop()` that `startLinuxTabPolicies` returns releases a held lock (and hands back one granted later), closes the channel and clears ownership.

### Page input counts as activity (#125)

- `startLinuxTabPolicies` subscribes the idle clock to `ArdiaEngine` `engine.inputActivity`, which the host sends for keyboard, mouse, wheel and touch input on page views and on any window's shell (native half in the page PR).
- Typing in a page or in another window now keeps memory trim from running.

### UPower and power profiles by signal (#127)

- `LinuxPowerMonitor` watches four objects with `dbus::PropertySet` on its own private system bus connection with a dedicated D-Bus runner. It uses its own connection because Chromium's shared bus caches one DisplayDevice proxy that the Battery Status API detaches when its last user goes away: UPower `OnBattery`, the `DisplayDevice` (`Type`, `IsPresent`, `Percentage`), and `ActiveProfile` from `org.freedesktop.UPower.PowerProfiles` or `net.hadess.PowerProfiles`.
- It runs `GetAll` at start and follows `PropertiesChanged` and invalidations afterwards. When a service gets a new owner it fetches again; when the service goes away the values become invalid. Changes reach `engine.systemState` listeners at once.
- The first answer waits for every `GetAll` reply or 2 s. Without UPower or a system bus, power and battery still report `unavailable`.
- Only the memory sample is still periodic: a `/proc` read every 30 s on the blocking sequence, with no D-Bus.
- The pure `LinuxPowerTracker` holds the merge, readiness and change-only notification, and is fixture-tested.

### Cast and capture controls (#123)

- `hostCast.ts` answers the media domain's cast and capture verbs in the shell:
  - `showCastDialog` shows "Casting Isn't Available" in the tab's window and resolves `false`;
  - `watchCastRoutes` resolves with no routes;
  - `startCasting`, `stopCasting`, `closeCastDialog`, `terminateCastRoute`, `changeCaptureSource` and `stopCapture` reject with `ERR_UNAVAILABLE`.
- Every other media verb still reaches the host. The toast and store load lazily, so the shim stays free of shared page state (`test:graph`).
- On Linux, display capture goes through Chromium's own portal picker and its own sharing notification (which has Stop sharing). The shared share bar and Ardia's picker never appear.

## Contract

| Item | Contract |
| --- | --- |
| Web Lock | `ardia.linux.tab-policies`, held for the owner window's lifetime |
| BroadcastChannel | `ardia.linux.tab-policies`, messages `{mode, reply}` from the owner |
| `media.showCastDialog` | `false` plus a toast |
| `media.watchCastRoutes` | `null` |
| Other cast and capture-switch verbs | `ERR_UNAVAILABLE` |
| `engine.systemState` | same shape; changes arrive on D-Bus signals instead of a 30 s poll |

## Tests run

| Command | Result |
| --- | --- |
| `python3 -m unittest tests.test_media_policy tests.test_power_bus tests.test_bootstrap_build tests.test_page_state tests.test_page_messages tests.test_tab_glue` | 55 passed |
| `scripts/linux/syntax-check-156.sh ac_linux_power ac_linux_tab_policy ac_linux_host.cc` | 3 of 3 pass |
| `pnpm --filter @ardia/linux typecheck` | passed |
| `pnpm --filter @ardia/linux test:host` | 425 passed |
| `pnpm --filter @ardia/linux test:graph` | 3 passed |
| `pnpm --filter @ardia/linux build` | passed; the bundle routes tabLifecycle's toasts and battery saver through the adapters |
| `pnpm --filter @ardia/linux acceptance:self-test` | 68 passed (the stock-Chromium F09 extension check failed once and passed on re-run) |

New cases: `hostPolicies.test.mjs` (8, including the reproduced stale-fallback race), `test_power_bus.py` (2), `hostCast.test.mjs` (2), `power-tracker` in `test_media_policy`. Each failed before its change. The F18 scenario gains the input-activity and single-owner checks.

## Needs native runtime acceptance on a Chromium 156 build

None of this has been compiled or run against Chromium 156:

- The `dbus::PropertySet` subclass and the private bus pass `scripts/linux/syntax-check-156.sh`; a GN check, compile and link have not run.
- UPower and power-profiles-daemon signals on a laptop: unplugging and switching to power-saver change `engine.systemState` at once.
- A system bus or UPower that is missing in the acceptance environment still answers within 2 s.
- With two windows, exactly one holds the lock, and closing it hands the trim to the other.
- One Battery Saver toast appears, in the focused window.
- `engine.inputActivity` arrives while typing in a page (it also needs the page PR).

## Left out (tracked)

- #170: memory pressure still comes from the MemAvailable ratio (Chromium 156 has no Linux memory-pressure evaluator).
- #171: real casting (Media Router) and switching the shared tab.
- #124 (manual PiP of an element or document PiP without a Media Session video) is not addressed here and stays open.
