# Linux media controls, PiP and tab policies (lane M)

Branch `t3/linux-media-policies`. Source-level implementation for #25 F15 and #28 F18, tracked by #116. No Chromium 156 compile, GN check, link or runtime run was possible in this lane: everything below is source-level and fixture-level evidence.

## What changed

### Native (`engine/ardiacore-linux/src/ardia/linux`)

- `ac_linux_media.{h,cc}` (pure, fixture-tested): Media Session sample → `nowPlaying` mapping (text bounded to 1024 UTF-16 units, artwork only `http(s)`/`data:image/` up to 1 MiB, at most 32 deduplicated action names, position clamped to the duration, non-positive or infinite durations become `null`); payload builders for `media {playing, muted}`, `nowPlaying {state}`, `mediaAccess {camera, microphone, screen}` and `pictureInPicture {kind, active}`; `LinuxMediaReporter`, which emits only changes (a re-sampled `nowPlaying` whose position matches the projection from the previous one within 0.5 s is not re-sent) and produces the snapshot list; `mediaCommand` parsing (`play|pause|toggle|next|previous|seekBy|seekTo|stop`, finite seconds within a year, `seekTo` ≥ 0) and its resolution against playback; control-press parsing and PiP routing (open → `true`; no activation or no session video → `false`; otherwise Media Session).
- `ac_linux_tab_policy.{h,cc}` (pure, fixture-tested): the protection predicate. A tab is protected when it is visible, audible, playing media, capturing (user media, tab/window/display, mirrored or being captured), in PiP, has a pending prompt (tab-modal dialog, including JS alert/confirm/prompt, which views shows through `WebContentsModalDialogManager`, or a permission request) or unsaved input. Any unknown field protects (`"unknown <field>"`). Discard decisions (`unsupported` for `{unload: true}`, `already`, `refused`, `discarded`), freeze gating, the tri-state unsaved-input rule (a beforeunload handler in any frame through `GetSuddenTerminationDisablerState(kBeforeUnloadHandler)`, PM form interaction, PM user edits; any true wins, any unknown is unknown), and `engine.systemState` from a UPower/power-profile/memory sample.
- `ac_linux_page_media.{h,cc}`: `LinuxPageMedia` per `TabBinding` contents. It observes audibility and mute, the Media Session (mojo `MediaSessionObserver`) and `MediaStreamCaptureIndicator`; dispatches `mediaCommand`, `requestPictureInPicture`, `takeControlPress`, `exitPictureInPicture` and `pictureInPictureState`; gathers `Protection(visible)` and `UnsavedInput()`. Manual PiP consumes the shell frame's transient user activation and a Media Session video player; it calls `MediaSession::EnterPictureInPicture()` and resolves when PiP opens or after 1.5 s. A pending request resolves `false` if it is superseded or the contents go away.
- `ac_linux_power.{h,cc}`: `LinuxPowerMonitor` samples the system bus every 30 s on a `MayBlock` sequence: UPower `OnBattery`, the `DisplayDevice` `Type`/`IsPresent`/`Percentage`, `ActiveProfile` from `org.freedesktop.UPower.PowerProfiles` or `net.hadess.PowerProfiles`, and `MemAvailable`. It notifies subscribers only on change. Without UPower it reports `power: "unavailable"`, `battery: "unavailable"`, `onBattery: false` and `batteryLevel: null`.
- `ac_linux_page_state`: `SetSnapshotProducer`. A producer's events are appended after navigation/security/zoom/favicon (names ≤ 64 characters, 128 events in total). The producer is cleared on retire and is reentrancy-safe.
- `ac_linux_host.cc`:
  - `TabBinding` owns `media`, `frozen` and `discard_reported`. It is created in the constructor and in `Rebind`, and dropped on contents destruction.
  - `MountPage` installs the producer `SnapshotExtras()`: the media snapshot plus `discarded {url}` while the contents are discarded. This sends the shared `onDiscarded` that #63 left out, both for in-place discards and in the rebind snapshot after an urgent-discard swap.
  - `WasDiscarded` emits `discarded`. The next primary navigation (Chromium clears `was_discarded_` when the reload starts) emits `ready`.
  - New tab commands: `discard` (`TabBinding::Discard`), through `TabLifecycleUnitExternal::DiscardTab(EXTERNAL)` after the protection predicate (the WebContentsDiscard in-place path keeps the Ardia tab); `setFrozen` (`TabBinding::SetFrozen`), which refuses `true` when protected and never calls `SetPageFrozen(false)` on a visible tab, because Chromium clears an explicit freeze on show; `unsavedInput`; and `downloadImage`, for artwork (http(s)/data only, 1–512 px, PNG data URI, resolves `null` if the download is dropped).
  - `SetPresentations` calls `Reveal()` on newly shown tabs: it clears the frozen flag, calling `SetPageFrozen(false)` only while the tab is not visible, and runs `LoadIfNecessary` for discarded ones.
  - The PiP window-manager observer now updates the binding's reporter, so the event is deduplicated and kept in snapshots.
  - `engine.call` intercepts `engine.systemState` and subscribes the window to `{name: "engine.systemState"}` events on `ArdiaEngine/engine`. The bootstrap advertises `engine.systemState`.
- `BUILD.gn`: the new sources, plus `//chrome/browser/media/webrtc`, `//chrome/browser/resource_coordinator`, `//components/performance_manager`, `//components/permissions`, `//dbus`, `//mojo/public/cpp/bindings` and `//services/media_session/public/{cpp,mojom}`.

### Linux UI (`apps/linux`)

- `main.tsx` starts the shared `startTabLifecycle` and `startMemoryTrim` once per browser-window renderer, not in utility windows (`hostPolicies.ts`). It provides a synchronous `ArdiaEngine.inputIdleSeconds` from shell input events. It also exposes `ardiaLinux.webviews`, `media`, `pages` and `lifecycle {state, sleepTab}` for the acceptance suite.
- `tabKeepAlive.ts` (a Vite adapter for the shared module) keeps `shownTabIds`/`keepAliveReason` and answers `hasUnsavedInput` from the native `unsavedInput` command. Anything that is not a boolean, an error or a 2 s timeout gives `null`, which protects.
- `hostPageScripts.ts` maps the two shared literals that need page script (the `PIP_STATE` probe and the document-PiP close) to `pictureInPictureState` and `exitPictureInPicture`. A test fails if the shared literals change.
- `hostPageLifecycle.ts`: a restore snapshot carrying `mediaAccess` replaces it.

## Contract

Page events (through the #63 lease): `media {playing, muted}`, `nowPlaying {state: NowPlaying|null}`, `mediaAccess {camera, microphone, screen}`, `pictureInPicture {kind, active}`, `discarded {url}` and `ready {}`. Every document, restore and rebind snapshot carries the first four, plus `discarded` while discarded.

Tab commands:

| Command | Result |
| --- | --- |
| `mediaCommand(action, seconds?)` | `null` |
| `requestPictureInPicture(press)` | boolean |
| `takeControlPress(press)` | boolean |
| `exitPictureInPicture()` | `null` |
| `pictureInPictureState()` | boolean |
| `discard({unload?})` | `"discarded" \| "already" \| "refused" \| "unsupported"` |
| `setFrozen(bool)` | boolean |
| `unsavedInput()` | `boolean \| null` |
| `downloadImage(url, maxPixels)` | `{uri, width, height} \| null` |

Engine calls: `engine.systemState` returns `SystemState` plus `power` and `battery` and emits the `engine.systemState` event.

## Tests run

| Command | Result |
| --- | --- |
| `python3 -m unittest tests.test_media_policy` | 7 passed (ASan/UBSan) |
| `python3 -m unittest tests.test_page_state` | 23 passed, including the new `producers` and `producer-lifetimes` |
| `python3 -m unittest tests.test_page_host_join` | 10 passed, including the new `media-producer`, `discarded-rebind` and `reveals` |
| `python3 -m unittest tests.test_auto_pip` | 2 passed (fake extended for `Reveal`) |
| `python3 -m unittest tests.test_tab_glue` | 5 passed. Compiles the real `Discard`, `SetFrozen`, `Reveal`, `WasDiscarded` and `DidStartNavigation`; the dropped-discard and visible-unfreeze cases fail without the round-1 fixes |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed |
| `pnpm --filter @ardia/linux typecheck` | passed |
| `pnpm --filter @ardia/linux test:host` | 322 passed, including `hostMedia.test.mjs` 6 |
| `pnpm --filter @ardia/linux test:graph` | 3 passed |
| `pnpm --filter @ardia/linux build` | passed; the bundle no longer contains the shared unsaved-input page script |
| `pnpm --filter @ardia/linux acceptance:self-test` | 49 passed, including the media fixture and the F15/F18 catalogue |

## Acceptance scenarios

`apps/linux/scripts/acceptance/scenarios/f15-media.mjs` and `f18-policies.mjs` use a local `/media` page with a looping WAV tone, Media Session metadata and handlers, and a form field. The checks and the manual items are listed in `acceptance-suite.md`.

## Needs native runtime acceptance on a Chromium 156 build

None of this has been compiled or run:

- GN visibility of the new deps.
- Media Session observer delivery and the shape of the `nowPlaying` it produces.
- Whether headless audio reports audibility.
- `MediaSession::EnterPictureInPicture` for a real video player.
- That `TabLifecycleUnitExternal` exists for the host's tabs. Without it, `discard` returns `refused`.
- In-place discard with `ready` on wake.
- A discard that Chromium drops because a navigation is about to commit (`frame_tree.cc` skips it, then still calls `NotifyWasDiscarded`) returns `refused`. `WasDiscarded` reports only when `contents->WasDiscarded()` is true, so a live tab is never shown as discarded.
- UPower on a laptop, power-profiles-daemon, and a system bus that is missing in the acceptance environment.
- That auto-PiP placement and cancellation still behave as before.

## Left out (tracked)

- #123: capture stop/change-source and cast controls.
- #124: manual PiP without a session video player (element/document).
- #125: input idle only sees shell input.
- #126: tab policies run once per window renderer.
- #127: UPower is polled rather than signal-driven.
