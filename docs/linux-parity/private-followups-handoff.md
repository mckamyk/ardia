# Private-window follow-ups handoff (#148, #149, #150, #172, #173)

Branch `t3/linux-private-followups`, based on `linux` at `8f22774f`. Follows `q-private-services-handoff.md`. Nothing here has run in a Chromium 156 build; the last section lists what to run.

## #148 Window menu

Linux has no menu bar, so the Window menu opens from the window controls: right-click the close, minimise and maximise buttons.

- **Module.** `apps/linux/src/windowMenu.ts` builds the menu and handles the choice. `TrafficLights.tsx` opens it, and `ardiaLinux.windowMenu` exposes it to the acceptance suite.
- **Items.** These follow macOS's Window menu (`packages/shell/ios/MainMenu.swift`):
  - Minimize and Maximize. Maximize is Linux's name for Zoom.
  - Downloads, then Task Manager when the host advertises `engine.openTaskManager`.
  - Merge All Windows, enabled under the shared rule: at least two regular, non-small windows.
  - The window list, as AppKit's `windowsMenu` adds it.
- **Not carried over.** Arrange in Front and Keep Window on Top have no Linux implementation, so they're left out. The Profiles submenu is left out too; profiles have their own menu.
- **Window list.** The menu's own window comes first, ticked, under its own title. Next come the other regular windows from the shared store, titled with `windowTitle`; utility and Little Ardia windows are left out. Last are the private windows from `ArdiaShell.privateWindows`: ids with the ordinal each private window took when it was configured (the lowest free one, `NextPrivateOrdinal`). `privateWindowEntries` labels them "Private Window", "Private Window 2" and so on from that ordinal, so every window shows the same label for the same private window (#232).
- **Private titles.** A regular window never takes a private window's title, even if its store held one. A private window's own entry uses its own title.
- **Freshness.** The private-window directory is read each time the menu opens, so the list is never stale. If that read fails, the menu still opens with the regular windows.
- **Choices.** Choosing a window calls `ArdiaShell.focusWindow`, which #89 already allows for private windows. Commands go through the shared command path (`emitCommand`). Minimize and Maximize call the host's window methods. Any other id is ignored.

## #149 Extensions in private windows

- **Toolbar.** `apps/linux/src/extensionsStore.ts` replaces the shared `state/extensions/store.ts` in the Linux build (a `vite.config.ts` replacement, checked by `test:graph`). It re-exports everything and overrides `useWindowExtensions`, `windowExtensions` and `shownIn`. In those, a private window shows every enabled extension allowed in incognito, split or spanning. The shared rule, which needs `incognitoSplit`, stays as it is for macOS. Popups and side panels already ran natively: `extensionView.create` accepts the window's own private session, and Chrome hosts a spanning extension's view in the original profile.
- **Live updates.** These needed no change. The engine's `ActionWatch` sits on the original profile (`ac_extensions_watch_actions` resolves `GetOriginalProfile()`), and Chrome redirects `ExtensionActionDispatcher` from incognito to the original. So an update on an OTR tab is emitted as `extensions.action` for the original profile. It isn't tagged `offTheRecord`, and the host forwards it to every window of that profile, private windows included. The private renderer's extension profile is the original's (`privateSession`), so the shared bridge refreshes its action states. Before, it skipped spanning extensions only because of the toolbar rule. The event carries only `{id, kind}`, and badges stay per tab, so a regular window never shows an OTR tab's badge. `test_private_extension_actions.py` pins this behaviour.

## #150 Incognito links from popups, side panels and DevTools

The Linux hook from #71 (`g_ardia_linux_off_the_record_navigation`, still only for browser-initiated navigations with a user gesture) needs no separate path for these sources. `Browser::OpenURL` passes a null source to `OpenURLFromTab`, so the navigator hands the hook the window's active tab. An extension popup or side panel opens its links through the browser, and DevTools forwards page links to its inspected tab (`DevToolsWindow::OpenURLFromInspectedTab`). Both therefore arrive as an Ardia tab, and that tab's page opens the private window. The extension-surface path and the DevTools unwrapping added for #150 never ran and were removed in #232. If the window's active tab has no live Ardia page, the navigation is refused with `[ardia] off-the-record link refused: the window's active tab has no live Ardia page`. Chrome never opens an unmanaged window for it.

## #172 Private download removals

- **Engine.** On Linux only, `ac_downloads.cc` tags the `downloads.removed` of an OTR item with `offTheRecord` (`Removal`, under `BUILDFLAG(IS_LINUX)`). The macOS payload stays `{id}`.
- **Host.** `EngineEvent` takes the tag off and delivers the event only to windows whose page profile is off the record and whose original profile path is the event's `profile` (`linux_downloads::DeliversRemoval`). This also covers the removals the engine sends when the OTR profile is destroyed. Regular removals still reach every window.

## #173 Automatic PiP in private windows

This needed no behaviour change; the decision is recorded here. The issue assumed that Chrome enters automatic PiP on ASK and asks inside the PiP window. Chrome 156 does that only for regular profiles:

- `AutoPictureInPictureTabHelper::IsEligibleForAutoPictureInPicture` returns false for ASK on a profile that `IsPrimaryOTRProfileWithRegularParent()`.
- That check runs before `EnterAutoPictureInPicture`.
- Ardia's private windows run on `GetPrimaryOTRProfile`.

So, with automatic PiP on, a private window reads ASK and never enters automatic PiP. With it off, the global switch blocks every window. macOS behaves the same: it leaves automatic PiP entry to the same Chrome helper, and its `autoPictureInPictureIsChromes` check only attributes a PiP window that has opened; it isn't an entry gate. `test_auto_pip_profiles.py` now checks that:

- the helper still has that check;
- no patch lifts it or adds ALLOW or ASK overrides;
- the Linux host enters PiP itself only behind a verified control press.

## Tests

The new tests failed before their fixes:

- `test_private_downloads.py`: removal routing and the engine's Linux-only tag.
- `downloads_scenarios.cc`: `DeliversRemoval`.
- `test_private_links.py`: the hook compiled against fakes (the owning tab opens, a pageless or unknown source is refused), plus the Chromium source facts it relies on.
- `test_engine_event_routing.py` (#232): `EngineEvent` compiled against fakes, covering private removals and downloads routed only to the private windows that own them.
- `windowMenu.test.mjs` and `extensionsStore.test.mjs`.

`test_auto_pip_profiles.py` and `test_private_extension_actions.py` pin behaviour that was already right. See the PR for the full run.

## Needs a Chromium 156 build

1. Run `--only 148,149,150,172,173` (see `acceptance-suite.md`), plus F02-72, F02-89, F02-99 and F09 to catch regressions.
2. Check these by hand, as listed in `acceptance-suite.md`:
   - the Window menu opened from the window controls, and the focus change;
   - a spanning extension's button and popup in a private window;
   - Open Link in Incognito Window from an extension popup and from DevTools;
   - automatic PiP in a private window.
