# Lane B handoff: page-commands host join (#11 F01)

Branch `t3/linux-page-commands-join`, rebased onto `linux` at `ccf9dc04` (after #61 window lifecycle and #64 downloads/files). It carries the five unpublished F01 helper commits from `t3/linux-page-commands` (cherry-picked unchanged, in order) and joins them to the Linux host. The helper contract is in [f01-page-commands-handoff.md](f01-page-commands-handoff.md); this file covers only what the join adds. The page-command parts of that document's "parent integration changes" are now done.

## What the join does

### Dispatch (`ac_linux_host.cc`)

- `ShellHostView::TabCommand` resolves `ClaimedBinding(args)` first. For the five names `LinuxPageCommands::Handles` accepts (`find`, `stopFinding`, `print`, `runPageCommand`, `showDevTools`), it calls `PageCommand`. Every other command keeps the existing path.
- A page command for a missing or stale claim rejects with `{code:"ERR_PAGE_STALE_TARGET"}` instead of the plain "No such live tab" string. Other commands keep that string.
- `PageCommand` builds one `ardiacore::LinuxPageCommands` per binding when the first page command arrives. It stores it in `TabBinding::page_commands` and answers `done(true, null)` or `done(false, {code,message})`. Find counts never appear in the reply.

### Captured authority

`ShellHostView::PageCommandAuthority` is a value struct. It holds a weak host, a weak handler, the committed `WeakDocumentPtr`, the weak contents, the native tab id, the claim string and the renderer generation, all captured when the helper is built. `PageCommandBinding(authority)` returns the binding only while all of these still hold:

1. The host, handler, document and contents are all alive.
2. The host is a regular window, not a utility window, and still has its browser.
3. The captured document is still the shell's primary main frame, `OwnsHandler(handler, generation)` holds, and `handler->Authorized()` is true.
4. `Binding(id)` still points at the same contents, with the same non-empty claim and the same `renderer_generation`.
5. The contents is still in this host's tab strip.

The helper's own profile and DevTools-browser checks run on top of this. The check has no side effects and never returns a replacement binding's owner.

- `Owner` is `PageCommandBinding(authority) ? host->browser() : nullptr`.
- `Emit` is `EmitPageCommandEvent(authority, event)`. It repeats the check, then calls the captured handler's `SendEvent("ArdiaWebView", id, {type:"findResult", value:{count,active,final}})`. It never uses `ReportTab` or a fresh handler lookup.
- **Lane A re-point:** `EmitPageCommandEvent` is the only place a Find event is shaped and sent. To move Find onto the new page-event envelope (`{type:"pageEvent", event:{name,data}, claim, rendererGeneration, ...}`), change only that function. The captured authority already carries the claim and generation. HostWebView's routing table was not touched.
- `allow_native_ui` is `!ardiacore::BackgroundInstance()`, which comes from the existing launch configuration. Background instances refuse print, system print, save and DevTools with `ERR_PAGE_UNAVAILABLE`. Find still works.
- `Presentation` repeats the authority check and calls `PresentDevTools(contents)`.

### Reset points

`TabBinding::ResetPageCommands()` moves the helper out of the binding and calls its terminal `Reset()`. It then deletes the helper with `DeleteSoon`, so a reset from inside `Dispatch` or a Find emit never frees the running object. It is called before:

- `tab.release`, including temporary disposal.
- `CreateTab` assigning a new claim (reclaim, adoption or transfer).
- `TabBinding::MoveTo`.
- `TabBinding::Rebind`.
- `RetireRenderer` clearing claims.
- `~TabBinding`, which covers host teardown and `DetachBrowser`.
- `TabBinding::WebContentsDestroyed`.
- `OnTabStripModelChanged` when a helper's contents is no longer in the strip.

A failed reclaim returns before any reset, so the existing committed authority survives. When a helper is reset because its contents left the tab strip, the pane is re-laid out at once, so a quarantined docked frontend doesn't leave a blank split behind.

After `CreateTab` grants a claim (reclaim after release, renderer replacement, or a transfer into another window), `AdoptDevTools(binding)` runs. If the contents has a DevTools frontend that a revoked Ardia lease quarantined, the new helper re-adopts it under a fresh lease and native action `kAdopt`. The adoption never steals focus:

- A docked frontend is re-announced with `UpdateBrowserWindow()`, so the dock hook lays it out in its pane.
- An undocked window is shown with `ShowInactive()`.

Chrome-owned frontends, and frontends whose lease is still current, are never adopted. Background instances never adopt. This is the answer to the moved-tab question: DevTools follows its tab, as in Chrome and Dia. A page paused at a breakpoint therefore becomes visible again as soon as the same contents is reclaimed. A released tab that is never reclaimed keeps its frontend hidden until Chrome closes it with the inspected contents.

### DevTools presentation

The real dispatcher is installed: `StartLinuxHost` calls the patched `DevToolsWindow::RegisterLinuxDevToolsLifecycle()`. Linux also sets the existing shared hook `g_ardia_devtools_dock_changed`, which `chromium-window-docked-devtools.patch` already calls from `DevtoolsUIController::UpdateDevtools` on every platform. That hook fires on docked Show, on resizing-strategy changes (`SetInspectedPageBounds`), on docking changes and on frontend destruction.

The hook and the `Presentation` callback both post `PresentDevTools`, which re-resolves the host and binding and runs `LayoutPane`. `SetViewport` now records the pane's bounds and its paint and input shapes on the binding and calls `LayoutPane` too. `LayoutPane`:

- Asks `DevToolsWindow::GetInTabWebContents(contents, &strategy)` for a docked frontend. It only asks for a visible, currently claimed binding. The authority patch makes this call return nothing for a quarantined, Ardia-owned frontend.
- Splits the pane with `ApplyDevToolsContentsResizingStrategy`.
- Clips the page's paint and input shapes to the page rectangle.
- Hosts the frontend in a second per-binding `ViewportView` with its own clipped shapes, so shared overlays still cut through it.
- Detaches the frontend from Chrome's own `ContentsContainerView::devtools_web_view()` if Chrome attached it there.
- Orders the two views by `hide_inspected_contents()`.
- Re-shows the frontend's native window if it is hidden. A revoked lease hides it while it may still be attached, and re-attaching the same contents at the same bounds would not show it again.
- Moves focus into the frontend only on the first attach of a *new* frontend. Each binding keeps a `DevToolsFrontendWatch` (a `WebContentsObserver` on the frontend). A move or a renderer replacement keeps the same frontend, and does not count as new.
- When a pane is hidden (tab switch), it remembers whether the frontend had focus. Re-showing the same frontend restores that focus, and otherwise leaves focus where the tab's restoration put it.
- When the watched frontend is destroyed, the watch posts a re-layout through `Hosts()`. This finds the binding even after a move, and also covers a quarantined frontend whose destruction never fires the dock hook. `TabBinding::WebContentsDestroyed` hides the DevTools view and re-lays out the pane too.
- On close (no docked frontend), hides the DevTools view, restores the full page rectangle, and moves focus back to the page if the frontend had it. That focus goes through #61's `TabBinding::FocusFromHost`. Its `host_focusing_` guard keeps the host's own focus change from being reported as `pageFocus`, which would otherwise switch split panes when DevTools close. The other two focus moves (first attach, restore on re-show) focus the DevTools frontend. The frontend is a separate WebContents that the inspected tab's `OnWebContentsFocused` does not observe, so neither can report `pageFocus` for the page.

An undocked frontend keeps Chrome's own window and lifecycle, apart from the quarantine and re-adoption described above.

### Patch behaviour changed in review round 1

- Toggle closes a docked frontend only when it is not quarantined (`LinuxDevToolsState::Quarantined()`: Ardia-owned with a revoked lease). Toggle on a quarantined frontend behaves as Show: it re-adopts the same frontend and un-hides its native view, so console state survives. A Chrome-owned docked frontend still closes on toggle.
- `BindLinuxDevToolsLease` un-hides the native view when a current lease takes over a quarantined frontend.
- The Linux ready callback lives in its own slot, `linux_load_completed_callback_`. Revocation clears only that slot. Chrome's single `load_completed_callback_` (also used by app-window DevTools) is never overwritten or dropped, and it still runs once even when the load completes under a revoked lease.
- A stale frontend-close reply that overlaps an intercepted inspected-page close now passes the frontend's *actual* answer to Chromium's page branch. `RenderFrameHostImpl::DispatchBeforeUnload` coalesces a second dispatch while one is pending, so a single reply answers both the stale close and the page interception. Approval continues the page close, and a veto cancels it. The previous code forced a veto, which cancelled tab closes nobody had vetoed. The stale close itself still never closes the frontend, and a newer close retry is still posted.

### Build and patch series

- `engine/ardiacore-linux/src/ardia/linux/BUILD.gn` adds a `devtools_authority` source set that depends only on `//base` and `//content/public/browser`.
- The `linux` target gains the Find and page-command sources and the dependencies listed in the F01 handoff. `//chrome/browser/printing` is added under `enable_printing`; `//chrome/browser/pdf`, `//components/pdf/browser` and `//pdf:features` under `enable_pdf`.
- `chromium-linux-devtools-authority.patch` gains a `chrome/browser/devtools/BUILD.gn` hunk. Under `is_linux` it adds `public_deps += [ "//ardia/linux:devtools_authority" ]`, because `devtools_window.h` includes the authority header, and `deps += [ "//ui/aura" ]` for the native-view hide.
- The patch is selected for Linux only, in `engine/patches/series` right after `chromium-linux-document-close.patch`. The macOS manifest is unchanged, and its pinned hash test still passes.
- No other selected patch touches the four target files. Dry runs (`git apply --check` and `patch --fuzz=0`) apply all four with zero fuzz and zero offset. They ran against copies of the Chromium 156 files in `/tmp/ardia-f01-patch` and `/tmp/ardia-f01-156`.
- The lifecycle test now pins and copies `chrome/browser/devtools/BUILD.gn` as well. It asserts that the only change to that file is the `is_linux` block.

### apps/linux

No adapter change was needed. `HostWebView` already forwards the five methods through `HostTab.command()`, and its `EVENT_CALLBACKS` already routes `findResult` to `onFindResult`, which `TabWebView` passes to the shared `setFind`. `hostPageCommands.test.mjs` is now part of `test:host`. A new case runs the production `HostWebView` `receive` function on events delivered by the production bootstrap, and checks that Find results reach only their own pane's `onFindResult` and stop after dispose.

## Tests run

The pinned excerpts were copied read-only from `~/ardia-linux-build-156/chromium_git/chromium/src` into `/tmp/ardia-f01-156`; their hashes match the pins. Heavy runs used `nice -n 10`. `node_modules` were installed with the repo's pinned pnpm 11.1.3 from the local store under `flock /tmp/ac-pod.lockf`. `scripts/agent/locked` needs macOS `lockf`, which is missing on Linux. The lockfile is unchanged.

- `test_page_commands.py`: 22 pass. That is the original 12; 6 compiled joined-dispatch cases that splice the real `ac_linux_host.cc` methods into the fixture under ASan/UBSan (two panes, stale claim/generation/document/handler/strip/utility, background refusal, unknown/unsupported `runPageCommand`, re-adoption only of quarantined frontends of a current claim, terminal and reentrant reset); and 4 source and manifest checks (reset ordering, dispatch/adoption/strip relayout registration, GN targets, series selection). Two hand mutations were each killed: dropping the captured-claim check, and ignoring background mode.
- `test_pane_layout.py` (new): 7 pass. It compiles and runs the real `ShellHostView::LayoutPane`, `TabBinding::WebContentsDestroyed` and `DevToolsFrontendWatch` against mock views, aura and DevTools. Cases: split and z-order, focus policy, close-focus going through `FocusFromHost` without a `pageFocus` report (failed before the #61 rebase fix: `test_devtools_close_returns_focus_to_the_page_without_reporting_page_focus`), destroyed frontend, destroyed frontend after a move, destroyed inspected contents, hidden native frontend. The fixture also compiles #61's real `FocusFromHost` and `OnWebContentsFocused`. Five hand mutations, each reverting one round-1 fix, were all killed.
- `test_devtools_authority.py` with `ARDIA_F01_PINNED_SOURCE=/tmp/ardia-f01-156`: 35 pass, including the BUILD.gn hunk and zero-fuzz/zero-offset application of every changed hunk.
- Full native suite (`python3 -m unittest discover -s engine/ardiacore-linux/tests`): 288 pass on `ccf9dc04`. That includes #61's `test_window_lifecycle.py`, #64's `test_downloads.py`, and the close-fixture suites fixed by #67.
- `engine/patches/build/tests`: 19 pass. Before the series entry, the existing manifest check failed with `patches missing from series`.
- `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`: 7 pass.
- `pnpm --filter @ardia/linux test:host`: 232 pass, including the 4 page-command cases and the window-lifecycle, downloads, files and file-drop host tests.
- `pnpm --filter @ardia/linux typecheck`: passes.
- `pnpm --filter @ardia/linux build`: passes, 1205 modules.

pnpm ran as `npm exec --yes --package=pnpm@11.1.3 -- pnpm`, and the lockfile is unchanged.

## Review round 1 (independent Opus review)

Each finding was reproduced by a failing test before the fix:

1. Focus stolen on every re-show. Failing before: `test_pane_layout ...test_reshow_restores_the_tabs_previous_focus_and_only_new_frontends_take_focus`.
2. Toggle closed a quarantined frontend. Failing before: `test_devtools_authority ...test_toggle_readopts_a_quarantined_frontend_instead_of_closing_it`.
3. A hidden undocked window was never re-shown. Failing before: `...test_reclaim_readopts_quarantined_frontends_without_focus` (against a stub `AdoptDevTools()`).
4. Empty DevTools split left after a frontend or inspected contents died. Failing before: `test_pane_layout ...test_destroyed_frontend_hides_its_panel_and_restores_the_page` and `...test_destroyed_inspected_contents_hides_the_panel`.
5. Hidden native frontend left blank. Failing before: `test_pane_layout ...test_presenting_reshows_a_hidden_native_frontend`.
6. A stale close reply vetoed an inspected-tab close. Failing before: `test_devtools_authority ...test_stale_close_reply_cannot_veto_an_inspected_page_close`. The earlier `page-overlap-*-allow` and `page-cancel-new-owner` cases asserted the wrong veto; they now assert that approval continues the page close and a veto cancels it.
7. Chrome's load-completed callback was overwritten or dropped. Failing before: `test_devtools_authority ...test_chrome_load_completed_callback_is_preserved`.

None of the findings were disputed.


## Still needs native runtime acceptance

None of this has been compiled against real Chromium 156 headers or run. Another thread owns the builds, and this lane must not build. The fixtures substitute Chromium, base and views types, so they prove the logic, not the ABI, GN header checks, compositor stacking or input. On a matching fullhost156 build, with two real split panes, from both shell focus and page focus, someone still needs to confirm:

- Find counts, next, previous and dismissal reach the right pane's FindBar.
- Print preview and system print produce output for the correct pane.
- Save Page writes the page, and PDF save works.
- Show, Toggle, Inspect and Console are visible and usable when docked (bottom, right and left), when undocked, and with `hide_inspected_contents`. This includes splitter drags updating the layout, overlays and occlusions cutting through the frontend, focus moving in and coming back after close, a beforeunload veto keeping the frontend, and native modal keyboard ownership.
- A background instance refuses all native UI.
- Round-1 behaviours:
  - Switching tabs keeps focus where the tab left it.
  - Moving a tab with docked or undocked DevTools to another window, or reloading the shell, brings the same frontend back without focus. This includes a page paused at a breakpoint.
  - Toggle after a move re-shows the frontend with its console state.
  - Closing a tab while a DevTools close is pending completes, unless the frontend itself vetoes.
- `gn gen` and `gn check` pass with the new `//ardia/linux:devtools_authority` edge from `//chrome/browser/devtools`.

Also open:

- A pane that has never received a viewport cannot show docked DevTools until its first `tab.viewport`.
- The DevTools view uses the same 12 px corner radius as the page. No Dia capture has been compared yet.
- The independent after-change review the F01 handoff asks for has not been done for the join.
