# Linux shell methods (#252, #266)

The shared UI calls `ArdiaShell` through `packages/shell`, whose wrappers call the native module unconditionally (`void Shell.watchLeftEdge(...)`, `void Shell.setDragEscapeCapture(...)`). Linux used to resolve 18 `ArdiaShell` methods, plus two `ArdiaApp` developer hooks, to `undefined` unless the native host advertised them. The list was written when the shared code still used optional calls. After the upstream rebase those calls threw `is not a function`:

- **Ctrl+S (#252).** Hiding the sidebar mounts `SidebarDock`'s left-edge watcher; the throw unmounted the React tree and left the window blank.
- **Sidebar tab drags (#266).** `DragController.begin` calls `setDragEscapeCapture(true)` first. The throw stopped every reorder and tab-to-page split.
- **Same crash, not yet reported.** The tab-group panel (`saveKeyResponder`), animated window closes (`animateWindowClose`), top-strip drags (`raiseView`) and the cross-window drag preview (`dragPreview*`).

## What resolves now

`resolveHostModule` (`apps/linux/src/hostModules.ts`) no longer returns `undefined`. For `ArdiaShell`, an advertised native method wins. Otherwise `linuxShellMethod` (`apps/linux/src/shellMethods.ts`) supplies the Linux implementation. Anything else goes to the native host, which implements it or rejects it as unsupported.

| Method | Linux behaviour |
| --- | --- |
| `watchLeftEdge` | The DOM watcher in `shims/web/motion/leftEdge.ts` reports `leftEdge` window events. The host window fills the viewport, so with no hold the in-window 8 px strip in `SidebarDock` opens the peek. A peek's hold keeps it open while the pointer stays inside. |
| `setDragEscapeCapture` | Native (#326). The host advertises it, and while a window holds the capture, a bare Escape pressed in any Ardia window (shell or page focused) is consumed by that window's key pre-target handler and sent to the capturing window's shell as the `dragEscape` app event, as the app-wide macOS monitor does. A modal dialog over the window keeps its Escape. The capture is released when the drag ends, when the capturing shell's renderer is replaced and when its window closes. On an engine without it, a capture-phase `keydown` listener in the shell does the same, but only while the shell has keyboard focus. |
| `saveKeyResponder` / `restoreKeyResponder` / `discardKeyResponder` | The focused shell element is saved by token and refocused if still connected. If the shell had no focused element (a page had focus), the token is 0 and the group panel hands focus back to the page. |
| `raiseView` | Raises the element and `levels` ancestors above their siblings with a z-index one higher than theirs, and restores their own z-index afterwards. |
| `animateWindowClose`, `cancelFocusMove`, `replyToTerminate`, `dragPreview*` | Settle at once. Linux has no close animation, no pending native focus move and no terminate handshake, and draws no floating drag preview outside the window. While the host doesn't advertise `ArdiaShell.dragPreviewPage`, a tab drag takes no page picture for the preview either (#325): the Linux build gives `tabDrag.ts` `apps/linux/src/dragWebviews.ts` in place of `platform/webviews.ts`, whose `get` finds no web view, so the drag falls through to the (equally unshown) cached picture or placeholder without capturing the page. An engine that implements the preview gets the shared web views back. |
| `switcherPointerMoved` | `false` until `startLinuxShortcuts` registers its tracker. |
| `pickFiles` | Native. `LinuxPickFiles` (`ac_linux_files.cc`) opens the desktop's multi-file chooser (portal or GTK through `ui::SelectFileDialog`) and answers with `file://` URLs, as macOS does. A cancel answers an empty list, a background instance answers an empty list without a dialog, and only one chooser opens at a time. Engines that don't advertise it get the explicit rejection below. |
| `fetchBangList`, `startDictation`, `pickFiles` on an engine without the native chooser | Reject with `ERR_UNAVAILABLE`. |
| `ArdiaSystem.hapticTick` | Settles quietly; Linux has no haptics. It used to reject as unsupported on every cross-window drag. |

The shell shim's exports still cover `showMenu`, `setTrafficLightsCenter`, `setAppearance`, `confirm`, `prompt` and `claimFocus`.

## The audit

`apps/linux/src/shellMethods.test.mjs` reads the module map from `ShellNativeModules` and `ShellOptionalNativeModules` in `packages/shell/src/nativeModules.ts`. It then finds each module's type in `packages/shell/src` and lists its methods; the optional modules are `ArdiaCli` and `ArdiaUserScripts`.

- **Optional modules.** They are audited only when the host advertises them. Otherwise the Linux expo shim returns `null` for them and the shared callers fall back.
- **Callable.** Every method must resolve to a callable with the capabilities `ac_linux_host.cc` advertises.
- **ArdiaShell.** Each method must be implemented by the native host (`method == "ArdiaShell.<name>"`), by `linuxShellMethod`, or by a shell-shim export.
- **Every other module.** `resolveHostModule` marks its generic fall-through to the native host (`reachesNativeHostUnchecked`). A method that ends there must meet one of these:
  - It is advertised in the bootstrap.
  - Its name is routed in `engine/ardiacore-linux/src/ardia/linux`.
  - It is on the test's explicit allow-to-reject list: `DEV_HOOKS`, the macOS developer hooks, and `TRACKED_IN_305`, the Mac-only features tracked in #305.
- **Stale entries.** A list entry that stops falling through fails the test.

The test adds a fake `ArdiaApp.bogusAppMethod` and `ArdiaShell.bogusShellMethod` and expects the audit to report both. Adding the method to `AppModule` itself also fails the audit.

## Verification

- **Unit tests.** `pnpm --filter @ardia/linux test:host`, including `shellMethods.test.mjs` (12 tests) and `shellCapabilities.test.mjs`.
- **Native.** `engine/ardiacore-linux/tests/test_open_files.py` covers the chooser's wiring, and `scripts/linux/syntax-check-156.sh ac_linux_files.cc ac_linux_host.cc` passes. Open File… needs the next Chromium build for its end-to-end check.
- **End to end.** The desktop-session harness ran against the `ee23b331` engine with this UI in a private sway session:
  - **Ctrl+S.** Hides the sidebar with the window intact. A pointer at the left edge peeks it; Ctrl+S restores it. The shipped UI logs `crashed TypeError: Kt.watchLeftEdge is not a function` and blanks the window.
  - **Splits.** F20D's tab-to-page split passes.
  - **Reorder.** Dropping a row in the top quarter of the row above reorders the sidebar.

F20D's reorder check drops 11.5 px into a 34 px row, inside the shared targeting's "drop onto" band (25–75 % of a row). By design that never reorders, on macOS either.

Follow-ups: no drag preview card on Linux (#325), and Escape cancels a drag only while the shell has keyboard focus (#326).
