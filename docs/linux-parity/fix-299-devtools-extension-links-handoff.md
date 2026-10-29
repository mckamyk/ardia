# #299: DevTools "Open in new tab" for a page Ardia doesn't host

Issue: [#299](https://github.com/mckamyk/ardia/issues/299), a follow-up to #275 (fixed for Ardia tabs in #289). Branch `fix3-media-299`.

## Cause

`DevToolsWindow::OpenInNewTab` calls `OpenURL` on the inspected contents. #289 reports the tab that returns through the inspected tab's Ardia binding. Two cases had no Ardia binding to report through:

- **Extension popups and side panels** (`ExtensionViewHost`) pass `OpenURL` to their browser, the hidden Chrome browser behind an Ardia window. A tab is inserted into that browser's strip and returned, but the inspected contents isn't an Ardia tab, so the host only logged `DevTools opened a tab for a page Ardia does not host`. Nothing showed the tab and nothing could close it.
- **Background pages and service workers.** A background page's `ExtensionHost` doesn't open URLs, so `OpenURL` returns nothing; a service worker has no inspected contents at all. Chrome's fallback then added a tab through `ScopedTabbedBrowserDisplayer` to the profile's last active tabbed browser (or a new one) and never told anyone.

## Fix

- `chromium-linux-devtools-open-tab.patch` adds `g_ardia_linux_devtools_open_url(Profile*, const GURL&)`. When `OpenURL` opened nothing and the hook is set, DevTools hands the URL to Ardia and returns; Chrome's `ScopedTabbedBrowserDisplayer` fallback no longer runs on Linux with Ardia's host.
- `ac_linux_host.cc`:
  - `g_ardia_linux_devtools_opened_tab`: the inspected tab's binding first (#289). Otherwise the tab is adopted (`ReportOpened`, `adoptId: tab:<id>`) by a window of the tab's profile with a live page, preferring the window whose strip holds it (a popup's or side panel's own window). With no such window the tab is closed and the refusal logged.
  - `g_ardia_linux_devtools_open_url`: a window of the DevTools profile with a live page, the active one first, opens the URL as a foreground tab (`openWindow`, as a dropped link does). With none, the link is refused and logged; nothing is opened.
  - The window and page are picked by `PrivateLinkBinding`: the presented binding, else any live one.
- Private windows: the profile comparison is exact, so an off-the-record extension view's link opens in a private window of that profile, never in a regular one.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_devtools_open_tab` | 4 passed. The compiled fake now covers a popup's tab in its own window's strip, a tab outside every strip (adopted by a same-profile window), another profile's tab (closed), a background page and a service worker (opened in the active window, then any window), and another profile's link (refused). The patch test applies the patch to clean `HEAD` (`git show`), since the reference tree's working copy already carries the old version. Fails on `linux` |
| clang `-fsyntax-only` of clean `devtools_window.cc` plus the patch, with `obj/chrome/browser/devtools/devtools.ninja`'s flags | passes |
| `bash scripts/linux/syntax-check-156.sh ac_linux_host.cc` | 1 of 1 passes |

## Needs the next Chromium build

Load an unpacked extension; from DevTools on its popup, its background page and its service worker, right-click a link (or an `href` in Elements) › Open in new tab: each opens as an Ardia tab in a window of the extension's profile, and no hidden tab is left (`chrome.tabs.query({})` lists only Ardia's tabs).

## Follow-up: which window, and focus

The "active window first" pass rarely matched, because the user is in the DevTools window. Links went to the first window of the profile by id, and that window wasn't raised.

- `DevToolsLinkWindow` picks among windows of the profile with a live page. It takes the window whose strip holds the tab when there is one, otherwise the most recently activated window (`ShellHostView::activation_`, from a counter bumped in `OnWidgetActivationChanged`).
- The chosen window is focused through `WindowFocus().Focus`, as `ArdiaShell.focusWindow` does, so background instances still only show inactive.
- A tab that an Ardia window already shows as a tab is neither adopted again nor closed.

Tests: `test_devtools_open_tab` 4; the compiled fake now covers the holder over a more recent window, the most recent window, a window without a live page being skipped, focus counts, and a shown tab of another profile left open. Syntax check of `ac_linux_host.cc` passes.
