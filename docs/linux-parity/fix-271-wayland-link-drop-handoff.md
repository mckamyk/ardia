# #271: on Wayland a link dragged out of a page and dropped back on it opens a tab

Issue: [#271](https://github.com/mckamyk/ardia/issues/271), found by the desktop-session harness (#240, wave 2 pages lane, F16D). Branch `fix2-link-drop`.

## Cause

`LinuxLinkGestureGate` (#134, `t-page-followups-handoff.md`, "Link drops") treats a left press with no release as the drag source, because on X11 the release that ends a drag goes to the drag-and-drop client, not the page. `NoteLinuxDragEnded` runs from `SystemDragEnded` and records the drop only while that press is still open.

On Wayland Chromium sends the page a release itself. `WaylandWindow::OnDragSessionClose` runs the drag-finished callback and then `WaylandEventSource::ReleasePressedPointerButtons(this)`, which dispatches a synthesized left release to the source window right away. That happens inside `DesktopDragDropClientOzone::StartDragAndDrop`'s nested loop, so it reaches the page widget's input observer before `StartDragAndDrop` returns and `WebContentsViewAura::EndDrag` calls `SystemDragEnded`. `NoteRelease` closed the press, `DragSource()` was 0 when the drag ended, and `AcceptLinkDrop` answered "no drag from this page ended". That is the `mouseup` after `dragend` the harness saw.

## Fix (Linux native only)

- `LinuxPageChannel::OnInputEvent` asks the page's root window's `aura::client::DragDropClient` whether a drag is in progress when it sees a left release (Chromium's own `web_contents_view_aura.cc` uses the same check). `DesktopDragDropClientOzone` keeps its drag context until the nested loop returns, so the synthesized release is seen as ending a drag.
- `LinuxLinkGestureGate::NoteRelease(..., ends_drag)` ignores such a release: the press stays open for `NoteDragEnded`, and the release is not a click for Peek, split or modifier-click fallbacks.

X11, where no release reaches the page during a drag, behaves as before. The shared browser and macOS are unchanged.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_page_messages` | 9 passed; `link-drops` fails to compile on `linux` (no `ends_drag` argument), and its new checks assert that a drag-ending release keeps the drag source, the drop opens, and that release does not peek |
| `bash scripts/linux/syntax-check-156.sh ac_linux_page_channel ac_linux_link_gesture` | 2 of 2 pass |
| `test_page_channel_contract`, `test_private_links`, `test_page_world`, `test_page_commands`, `test_linux_features` | OK |

## Needs the next build to confirm

F16D's "dragging a link onto its own page with no drop target opens one foreground tab on the link" on sway, plus the drop-target and outside-window checks staying negative, against a package built from this branch.
