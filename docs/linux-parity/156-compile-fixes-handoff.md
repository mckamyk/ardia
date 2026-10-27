# Chromium 156 compile fixes (#157)

`scripts/linux/syntax-check-156.sh` runs `clang -fsyntax-only` over every Linux-built Ardia source
(`engine/ardiacore-linux/src/ardia/{linux,core}/**/*.cc` and the Linux `ardia_sources` of
`engine/chromium/src/chrome/browser/ardia/BUILD.gn`). It uses the flags from the 156 tree's
`obj/ardia/linux/linux.ninja`, `obj/ardia/core/launch_switches.ninja` and `obj/chrome/browser/ardia/ardia.ninja`,
and only reads the tree. Everything it generates goes under `--work` (default `/tmp/v157`):

- `gen/`: `ardia/linux/mojom/page.mojom*.h` made by the tree's own `mojom_parser.py` and
  `mojom_bindings_generator.py` (run with `-B`, cwd in the work dir), `page_script.h`, `host_bootstrap.h`,
  `ac_engine_dispatch.inc`, and the three `ardia/core` headers the Linux layer copies in.
- `overlay/`: copies of the tree files that the Linux-applicable `engine/patches/series` touches. Each hunk the tree
  doesn't have yet is applied to the copy. Each per-file section is classified as present (reverse applies),
  applied, or conflict (neither applies and its added lines are missing).

`--no-overlay` checks against the bare tree headers, to list what only the patch series provides. Run it with
`--jobs 4` or so while a Chromium build is running (it already runs under `nice -n 10`); a full pass takes about
45 s at 6 jobs.

## Fixed against the 156 APIs

| File | Error | Fix |
| --- | --- | --- |
| ac_linux_host.cc | `base/containers/contains.h` is gone | `std::ranges::find` (the fixture tests build as C++20, so not `std::ranges::contains`) |
| ac_linux_host.cc | `BrowserView::contents_web_view()` is gone (4 uses) | `GetActiveContentsWebView()`; the pane-layout fixture mock is renamed to match |
| ac_linux_host.cc | lvalue `ListValue::Append` returns void (tabs.stripCommand) | two `Append` statements |
| ac_linux_extension_service.cc | `ui::SelectFilePolicy` incomplete at `SelectFileDialog::Create` | include `select_file_policy.h` |
| ac_linux_extension_service.cc | `ListValue::GetList()` is gone | iterate the `ListValue` |
| ac_linux_extension_service.cc | `DeleteSoon` can't reach `FolderChooser`'s private destructor | `friend class base::DeleteHelper<FolderChooser>` |
| ac_linux_page_channel.cc | `NoDestructor` rejects the trivially destructible `LinuxLinkGestureGate` | plain function-local static |
| ac_linux_page_state.cc | `blink::mojom::FaviconURL` incomplete | include `favicon_url.mojom.h` (from `mojom_platform`, a public dep of `mojom_core`) |
| ac_linux_power.cc | `dbus::Bus::Options` is move-only | `std::move(options)` |

## Build-time-only gaps (not bugs; a real 156 build closes them)

- `DevToolsWindow::RegisterLinuxDevToolsLifecycle`: the 156 tree lacks `chromium-linux-devtools-authority.patch`.
- `ISOLATED_WORLD_ID_ARDIA_PAGE` (`chrome_isolated_world_ids.h`): the 156 tree lacks `chromium-linux-page-agent.patch`.
- `ardia/linux/mojom/page.mojom.h` and `ardia/linux/page_script.h`: not generated yet. The tree's `ardia/linux/BUILD.gn`
  is an older layer copy without `page_mojom`/`page_script`.
- `.cc`-only hunks the tree lacks, which don't affect the check: `chromium-linux-action-popup-hook`,
  `chromium-linux-page-status`, `chromium-linux-private-links`, `chromium-linux-navigation-hold-hook`, and the
  content-blocker hunk of `chromium-ardia-linux-layer`. The tree also carries an older `chromium-linux-auto-pip`
  hunk in `chrome_main_delegate.cc` (no `WebContentsDiscard`). The `chromium-main-frame-throttle-after-show`
  conflict is a later patch rewriting the same lines, not a real gap.

With the overlay, 94/94 files pass on linux at b606c476. Without it, three files fail, and only on the first two
gaps above. Runtime behaviour still needs a 156 build. In particular `GetActiveContentsWebView()` returns only
the active contents view, which matches the old `contents_web_view()` while Chrome's own split view is unused.
