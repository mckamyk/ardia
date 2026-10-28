# #251: bookmarks written by one window reach the other windows

Issue: [#251](https://github.com/mckamyk/ardia/issues/251), found by the desktop-session harness (#240, F12D). Branch `linux-html-bookmarks`.

## Cause

Each Linux window is its own renderer with its own store. Bookmarks reach the other renderers only through Chromium's bookmark model: the writer sends `bookmarks.apply`, and the model's `bookmarks.changed` event makes every other renderer re-read (`state/bookmarks/engineSync.ts`).

`ac_bookmarks_apply` in the shared engine mutes its own watcher while it applies ops (`set_applying(true)`). On macOS that's right, because one JS runtime made the edit and nobody else needs to hear about it. On Linux the write therefore never reached any other window. The Import window's HTML import landed in Chromium's model (the reply said `applied: 3`), but the browser window only saw it after a relaunch read the model again.

The Chrome and Firefox import paths call the same `importBookmarks` and push the same way, so they had the same gap. So did any edit made in one browser window while another was open.

Probe on the ee23b331 package: Settings' renderer adds a bookmark, and `bookmarks.apply` succeeds with `applied: 1`. `bookmarks.tree` from the main window has it, but the main window's store doesn't, even after 10 s.

## Fix (Linux host only)

- `ac_linux_contract.h`: `ChangeAnnouncedAfter(call, applied)` names the event to announce after a call. It is `bookmarks.changed` for a `bookmarks.apply` that applied at least one op, and nothing otherwise.
- `ac_linux_host.cc`: the generic engine route records the resolved profile path in `EngineReply`. After a successful reply, `Replied` sends that event, with that profile path, through `ShellHostView::EngineEvent` to every window except the caller. That is the same path and translation as an engine-emitted event, so private windows and other profiles filter it as before. The caller already holds the change, as on macOS.

The shared engine (`engine/chromium/.../ac_bookmarks.cc`) and macOS are unchanged.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_bookmark_changes` | 3 passed: the compiled contract helper (ASan/UBSan), the engine still muting during apply, and the host wiring. Two of them fail on `linux`. |
| `bash scripts/linux/syntax-check-156.sh ac_linux_host.cc` | ok |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | see the PR description |

## Needs the next build to confirm

This is a native change. On the next 156 package, run:

- F12D from the `acceptance/desktop-session` harness. "choosing an exported bookmarks.html puts its 3 links and its Work folder in the browser window" should pass without a relaunch.
- The quick probe: in a two-renderer instance (main window plus Settings), add a bookmark from Settings' store. It should appear in the main window's `s.bookmarks` within a second.
