# #265: history imported from another browser reaches the open windows

Issue: [#265](https://github.com/mckamyk/ardia/issues/265), F12W on 148104a3. Branch `linux-import-refresh`. Follows [#251](fix-251-bookmark-changes-handoff.md).

## Cause

On 148104a3, F12W's open browser window already had the imported bookmarks (the #251 fix: the Import window's `bookmarks.apply` is announced to the other windows), but no history.

The Import window writes history through `history.import`, which calls `HistoryService::AddPagesWithDetails`. That records one visit per row but notifies observers only through `OnURLsModified`, so the other renderers get a `modified` change. `applyHistoryChange` updates only URLs already in a window's list, so the imported rows never appeared. A relaunch re-reads history from Chromium, which is why they were there afterwards.

Building `visit` changes from the request isn't truthful: `AddPagesWithDetails` silently skips expired rows, and the host would have to copy Chromium's filters. A `deleted`/`all` change would make the windows re-read, but favicons also listens to it and forgets every icon. So the windows get their own signal.

## Fix (Linux only)

- `ac_linux_contract.h`: `ChangeAnnouncedAfter(call, applied)` now takes `std::optional<int>`. It returns `history.imported` for every successful `history.import`. `EventName` forwards `history.imported`, so `ShellHostView::EngineEvent` delivers it as module `ArdiaHistory`, event `history`, `{name: "history.imported", data: {profile}}`.
- `ac_linux_host.cc`: `Replied` passes the reply's `applied` as an optional. The #251 announcement then sends `history.imported`, with the written profile's path, to every window except the caller.
- `apps/linux/src/hostHistoryImports.ts`: `watchHistoryImports(host, reload)` calls the shared `reloadHistory()` when that event arrives. `hostData.ts` starts it with the other data services in every Linux renderer.

The shared dispatcher in `packages/ardiacore` routes events by name, so the shared code ignores `history.imported`. Nothing shared, and nothing on macOS, changes.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_bookmark_changes` | 4 passed; the new history test and the updated wiring test fail on `linux` |
| `python3 -m unittest test_contract test_downloads test_extensions test_bookmark_changes test_private_services` | 30 run, 1 failure: the pre-existing `test_action_popup_hook_patch_is_registered_for_linux_and_applies` dry run against the 156 tree, which already has that patch applied |
| `bash scripts/linux/syntax-check-156.sh ac_linux_host.cc` | ok |
| `node … --test src/hostHistoryImports.test.mjs` | 4 passed; fails on `linux` (module missing) |
| `pnpm --filter @ardia/linux test:host` | 592 passed |
| `pnpm --filter @ardia/linux typecheck`, `test:graph` | pass (graph: 3) |
| Probe, 148104a3 engine with this branch's dist | Settings imports 3 rows through `history.import`. Chromium has them; the main window has 0 after 3 s. After the event the patched host would send (injected through `window.__ardiaEvent`), it has all 3 within a second. With a dist from `linux`, the injected event changes nothing. 2 of 2 runs each way. |

## Needs the next 156 build

The host announcement is native. On the next package, run F12W: "the open browser window has the imported bookmarks and history" should pass without a relaunch. Close #265 then.
