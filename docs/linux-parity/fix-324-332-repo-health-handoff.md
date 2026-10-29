# Repo and test health: conventions (#324), native suite (#332), F22

## #324: `pnpm -C apps/browser test` conventions check

The check found three violations on `linux`. All three are fixed in code; the baseline stays empty.

- **`ac_passwords.cc` over 600 lines.** `ac_passwords_exceptions` and `ac_passwords_allow` moved to `ac_passwords_exceptions.cc`. They share `Session`, `Open`, `WithoutSlash` and the `AC_PASSWORDS_OPEN` macros through `ac_passwords_internal.h` (namespace `ardia::passwords_internal`, the same pattern as `ac_autofill_internal.h`). `PasswordsState` is unchanged; it moved from the anonymous namespace into `passwords_internal` so `Session` can name it. No behaviour changes on macOS or Linux, and the reveal call `test_secret_reply.py` reads stays in `ac_passwords.cc`.
- **`ARDIA_SCROLL_FULL_RATE` in `ac_input_frame_rate_linux.cc`.** It was one of the environment-only switches retired in 0.2.40 (`docs/kill-switches.md`); macOS no longer reads it. Linux now matches macOS: the full-rate-on-input observer always starts.
- **`XDG_RUNTIME_DIR` and `HYPRLAND_INSTANCE_SIGNATURE` in `pip/ac_pip_hyprland.cc`.** The Linux launch config (`InitializeLinuxLaunchConfig`, the one place Linux reads the environment) copies them into `--ardia-hyprland-instance` and `--ardia-hyprland-runtime-dir` when the signature is set and not empty. The PiP code reads them with `ardiacore::InstanceValue`. A value already on the command line wins, and child processes never import them. `HyprlandSocketPath(runtime, signature)` is now a pure function and still refuses a relative runtime directory and any signature outside `[A-Za-z0-9_-]`.

Tests: `test_launch_switches.py` covers the environment copy, command-line precedence, an empty signature and renderer processes. `test_pip_hyprland.py` covers the pure path builder and checks that the file has no `getenv`. `scripts/linux/syntax-check-156.sh` passes for every changed file.

## #332: native suite on a build machine whose 156 tree is already patched

The native suite had 37 failures and errors on `148104a3`. There were two causes.

- **Patch dry-runs against the live tree (9 tests).** `test_autoplay_policy`, `test_background_windows`, `test_devtools_open_tab`, `test_extension_followups` (3), `test_extensions`, `test_site_controls` and `test_web_store` copied files from `~/ardia-linux-build-156/chromium_git/chromium/src`, which already has the series applied. They now use `tests/chromium_series.py`:
  - `applied(patch, *files)` starts each file from the Linux series base (`AC_SERIES_BASE`, by default `series-base-linux-x64` next to `chromium_git`). A file missing from the base comes from the tree's git `HEAD`, which is pristine Chromium because the series isn't committed. This is the same fallback `series.py materialize` uses.
  - It then applies, with `git apply --include`, every earlier Linux step of `engine/patches/series` that touches those files, so a patch is checked against the context it really meets.
  - Finally it applies the patch under test with `patch -F0` and yields the directory.
  - Without a tree or a base the tests skip; with `HOME` pointing at an empty folder, the converted tests run 55 with 11 skipped and none failed.
- **The shared host fixture stopped compiling (28 errors and failures).** `document_host_fixture.py` stands in for `ShellHostView` and compiles the real `OnWidgetClosing` and `CommitWindowDeparture`. Those now call `RememberWindowOutput()` and `ForgetWindowOutput()` (#249, #268, #314), which the stand-in lacked. It now has counting stubs. This one break took down `test_dialog_dispatch`, `test_document_close_*`, `test_document_host`, `test_document_renderer`, `test_utility_close` and `test_window_storage`. The `FileExistsError`s in `test_document_close_deliberation` followed from the first failed compile in its shared scratch folder.

The syntax checker's overlay also reported a false `CONFLICT` for `chromium-main-frame-throttle-after-show.patch` on `cc/scheduler/scheduler_state_machine.cc`. `chromium-main-frame-full-rate-on-input.patch` rewrites part of what it adds, so the earlier patch neither reverse-applies to the tree nor has all of its added lines there. Before reporting a conflict, `syntax_check_156.py` now applies every Linux step touching the file to its series-base copy and compares the result with the tree. Here they match byte for byte, so nothing is reported.

## F22 retired in favour of F22D/F22X

See `acceptance-suite.md` › Desktop sessions › F22 retired. In short, F22's nested sway had no keyboard device, so no window could ever have keyboard focus. Its four checks failed on `148104a3` while F22D and F22X passed. Its one unique check, a window the app opens in the background, moved into F22D/F22X and passes on both. Its Import step joined their manual list.
