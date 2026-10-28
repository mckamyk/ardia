# #259: clicking the inactive split pane activates it and keeps its keyboard focus

Issue: [#259](https://github.com/mckamyk/ardia/issues/259), found by the desktop-session harness (#240, wave 2 pages lane, F01D). Branch `fix2-split-pane-focus`.

## Cause

Clicking pane A while pane B is active gives A's page focus, and `TabBinding::OnWebContentsFocused` reports `focus`; the shell's `activateFocusedPane` activates A, and the host's `activate` strip command calls `TabStripModel::ActivateTabAt`. Chrome's `BrowserView::OnActiveTabChanged` then does what it does for any tab switch in an active window:

1. `old_contents->StoreFocus()`: B's `ChromeWebContentsViewFocusHelper` remembers the focused view, which is now A's viewport.
2. `FocusManager::ClearFocus()`.
3. `new_contents->RestoreFocus()`: A's helper focuses the view it stored when A was last deactivated. In a split that was B's viewport, stored the same way when B was clicked.

So A's activation focused B's page, B reported `focus`, the shell activated B again, and the same restore bounced focus once more. A trace of pane A's and pane B's `window` events with the store's `activate` calls on the 0f5d71dc package (sway, real pointer): A `focus`, `mousedown`, `blur`; `activate(A)`; B `focus`; `activate(B)`; B `blur`; nobody kept keyboard focus, and the active tab stayed B.

Chrome's own split view skips this (`IsTabChangeInSplitView`), but Ardia's panes are separate tabs with their own `ViewportView`s, so every pane switch is an ordinary tab switch to Chrome.

## Fix (Linux native only)

`ShellHostView::KeepPaneFocus` keeps each tab's stored focus inside its own pane:

- Before the shell's `activate` calls `ActivateTabAt`, a pane whose page or docked DevTools already has focus (the click that started the activation) stores that view, so Chrome's restore keeps it there.
- Before that and after every active-tab change (`OnTabStripModelChanged` with `active_tab_changed()`), any tab whose stored focus is another tab's viewport or DevTools viewport stores its own viewport instead. Chrome's `StoreFocus` on the tab being left is what records the neighbour's viewport.

Stored focus on the shell (a sidebar click) or on nothing is left as Chrome set it, so switching tabs from the sidebar behaves as before. This doesn't touch typeahead or the window focus ledger (#267): it only changes which view Chrome restores inside an already active window.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_pane_focus` (new) | 4 passed. The fixture emulates Chrome's `StoreFocus`/`ClearFocus`/`RestoreFocus` on a tab switch; with `KeepPaneFocus` stubbed out, `click`, `sidebar` and `devtools` fail (focus lands on the other pane's viewport) |
| `bash scripts/linux/syntax-check-156.sh ac_linux_host.cc` | pass |

## Needs the next build to confirm

F01D's "clicking into the inactive split pane makes it active and gives its page keyboard focus" on sway, against a package built from this branch, plus the F01D shortcut checks that start by clicking a pane.
