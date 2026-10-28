# DevTools "Open in new tab" reaches Ardia's window (#275)

DevTools' Open in new tab (an `href` in Elements, a Sources or Network link) calls `DevToolsWindow::OpenInNewTab`. That calls `OpenURL` on the inspected tab with browser-initiated params, so there is no source frame. `WebContentsImpl::OpenURL` only notifies `DidOpenRequestedURL` when a source frame exists. Ardia's tab binding never heard of the tab Chrome inserted into the window's strip: the tab was active in Chrome's model (the window title changed) but no Ardia tab showed it. Regular and private tabs took the same path.

- `engine/patches/chromium-linux-devtools-open-tab.patch` (Linux only, after `chromium-linux-devtools-authority.patch`) defines `g_ardia_linux_devtools_opened_tab(inspected, opened, url)` and calls it when `OpenURL` returns a tab other than the inspected one. Chrome's own fallback (`ScopedTabbedBrowserDisplayer`) still runs when `OpenURL` opens nothing.
- `ac_linux_host.cc` sets the hook. The host holding the inspected tab reports the new tab through that tab's binding with `ReportOpened(opened, url, "foreground")`, exactly as a page's `target=_blank` link does. So the shared `openFromPage` path adopts it (`adoptId: tab:<id>`) in the same window and profile, private windows included. `ReportOpened` refuses a tab from another browser context. A tab whose inspected page Ardia doesn't host is logged.
- `engine/ardiacore-linux/tests/test_devtools_open_tab.py` compiles the patch's Linux block, the hook and `ReportDevToolsOpenedTab` against fakes. It also dry-runs the patch against the 156 tree.

Needs the next Chromium build. The end-to-end check is the wave-2 profiles scenario for F02-150 (#150): Open in new tab from DevTools on a private tab adds a tab to the private window's sidebar.
