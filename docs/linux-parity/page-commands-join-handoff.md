# Lane B handoff: page-commands host join (#11 F01)

Branch `t3/linux-page-commands-join`, based on `linux` at `4b3d21e0`. It carries the five unpublished F01 helper commits from `t3/linux-page-commands` (cherry-picked unchanged, in order) and joins them to the Linux host. The helper contract is in [f01-page-commands-handoff.md](f01-page-commands-handoff.md); this file covers only what the join adds. The page-command parts of that document's "parent integration changes" are now done.

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

A failed reclaim returns before any reset, so the existing committed authority survives.

### DevTools presentation

The real dispatcher is installed: `StartLinuxHost` calls the patched `DevToolsWindow::RegisterLinuxDevToolsLifecycle()`. Linux also sets the existing shared hook `g_ardia_devtools_dock_changed`, which `chromium-window-docked-devtools.patch` already calls from `DevtoolsUIController::UpdateDevtools` on every platform. That hook fires on docked Show, on resizing-strategy changes (`SetInspectedPageBounds`), on docking changes and on frontend destruction.

The hook and the `Presentation` callback both post `PresentDevTools`, which re-resolves the host and binding and runs `LayoutPane`. `SetViewport` now records the pane's bounds and its paint and input shapes on the binding and calls `LayoutPane` too. `LayoutPane`:

- Asks `DevToolsWindow::GetInTabWebContents(contents, &strategy)` for a docked frontend. It only asks for a visible, currently claimed binding. The authority patch makes this call return nothing for a quarantined, Ardia-owned frontend.
- Splits the pane with `ApplyDevToolsContentsResizingStrategy`.
- Clips the page's paint and input shapes to the page rectangle.
- Hosts the frontend in a second per-binding `ViewportView` with its own clipped shapes, so shared overlays still cut through it.
- Detaches the frontend from Chrome's own `ContentsContainerView::devtools_web_view()` if Chrome attached it there.
- Orders the two views by `hide_inspected_contents()`.
- Focuses the frontend when it is newly attached.
- On close (no docked frontend), hides the DevTools view, restores the full page rectangle, and moves focus back to the page if the frontend had it.

An undocked frontend keeps Chrome's own window and lifecycle.

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

- `test_page_commands.py`: 21 pass. That is the original 12, plus 5 compiled joined-dispatch cases that splice the real `ac_linux_host.cc` methods into the fixture under ASan/UBSan (two panes, stale claim/generation/document/handler/strip/utility, background refusal, unknown/unsupported `runPageCommand`, terminal and reentrant reset), plus 4 source and manifest checks (reset ordering, dispatch registration, GN targets, series selection). Two hand mutations were each killed: dropping the captured-claim check, and ignoring background mode.
- `test_devtools_authority.py` with `ARDIA_F01_PINNED_SOURCE=/tmp/ardia-f01-156`: 31 pass, now including the BUILD.gn hunk.
- Full native suite (`python3 -m unittest discover -s engine/ardiacore-linux/tests`): 171 run, 166 pass. The 5 failures are the pre-existing close-fixture ones listed below; the `linux` baseline has the same 5 plus the extension-contract test, which only passes with `node_modules` installed.
- `engine/patches/build/tests`: 19 pass. Before the series entry, the existing manifest check failed with `patches missing from series`.
- `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`: 7 pass.
- `pnpm --filter @ardia/linux test:host`: 206 pass, including the 4 page-command cases.
- `pnpm --filter @ardia/linux typecheck`: passes.
- `pnpm --filter @ardia/linux build`: passes, 1199 modules.

The failures that remain also fail on `linux` itself (checked by running the suite on a `git archive` of `linux`). `test_chromium_close.ChromiumCloseTests`, `test_document_close_{admission,cap,stage}` and `test_chromium_close...test_minimal_target_and_linux_gated_dependencies` all fail because `chromium-linux-document-close.patch` (rebased in #60) no longer applies to the frozen `chromium156-close.tar.xz` fixture at `unload_controller.{h,cc}`. That belongs to the owner of the close patch or fixture, not this lane.

## Still needs native runtime acceptance

None of this has been compiled against real Chromium 156 headers or run. Another thread owns the builds, and this lane must not build. The fixtures substitute Chromium, base and views types, so they prove the logic, not the ABI, GN header checks, compositor stacking or input. On a matching fullhost156 build, with two real split panes, from both shell focus and page focus, someone still needs to confirm:

- Find counts, next, previous and dismissal reach the right pane's FindBar.
- Print preview and system print produce output for the correct pane.
- Save Page writes the page, and PDF save works.
- Show, Toggle, Inspect and Console are visible and usable when docked (bottom, right and left), when undocked, and with `hide_inspected_contents`. This includes splitter drags updating the layout, overlays and occlusions cutting through the frontend, focus moving in and coming back after close, a beforeunload veto keeping the frontend, and native modal keyboard ownership.
- A background instance refuses all native UI.
- `gn gen` and `gn check` pass with the new `//ardia/linux:devtools_authority` edge from `//chrome/browser/devtools`.

Also open:

- A pane that has never received a viewport cannot show docked DevTools until its first `tab.viewport`.
- The DevTools view uses the same 12 px corner radius as the page. No Dia capture has been compared yet.
- The independent after-change review the F01 handoff asks for has not been done for the join.
