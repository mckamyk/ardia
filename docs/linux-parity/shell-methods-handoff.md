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
| `setDragEscapeCapture` | A capture-phase `keydown` listener turns a bare Escape into the `dragEscape` app event and consumes it, as the macOS monitor does. |
| `saveKeyResponder` / `restoreKeyResponder` / `discardKeyResponder` | The focused shell element is saved by token and refocused if still connected. If the shell had no focused element (a page had focus), the token is 0 and the group panel hands focus back to the page. |
| `raiseView` | Raises the element and `levels` ancestors above their siblings with a z-index one higher than theirs, and restores their own z-index afterwards. |
| `animateWindowClose`, `cancelFocusMove`, `replyToTerminate`, `dragPreview*` | Settle at once. Linux has no close animation, no pending native focus move and no terminate handshake, and draws no floating drag preview outside the window. |
| `switcherPointerMoved` | `false` until `startLinuxShortcuts` registers its tracker. |
| `fetchBangList`, `startDictation`, `pickFiles` | Reject with `ERR_UNAVAILABLE`. |
| `ArdiaSystem.hapticTick` | Settles quietly; Linux has no haptics. It used to reject as unsupported on every cross-window drag. |

The shell shim's exports still cover `showMenu`, `setTrafficLightsCenter`, `setAppearance`, `confirm`, `prompt` and `claimFocus`.

## The audit

`apps/linux/src/shellMethods.test.mjs` reads the method types the shared native modules declare in `packages/shell/src` (`ShellNative`, `AppModule`, `SystemNative`, `CalendarNative`, `KeychainNative`, `TranslationModule` and the rest). Every declared method must resolve to a callable on Linux with the capabilities `ac_linux_host.cc` advertises. Every `ArdiaShell` method must also be implemented by the native host (`method == "ArdiaShell.<name>"`), by `linuxShellMethod`, or by a shell-shim export. A method added to the shared types without a Linux path fails this test.

These still reach the native host and reject as unsupported Linux host methods:

- **Updater, Dock and app icons:** `ArdiaApp.updaterState` and friends, `isInDock`, `addToDock`, `appIcons`.
- **Sharing and notifications:** `share`, `shareVia`, the notification methods.
- **Scripts and dev hooks:** `setScriptState`, `replyToScript`, `ArdiaApp.dev*`.
- **Other modules:** `ArdiaKeychain.*`, `ArdiaTranslate.*`, `ArdiaInlineCompletion.complete`, `ArdiaDockSelection.iconTheme`.

Shared callers handle the rejection; they are tracked as a follow-up rather than given silent fallbacks.

## Verification

- **Unit tests.** `pnpm --filter @ardia/linux test:host`, including `shellMethods.test.mjs` (9 tests; 7 fail against the previous `hostModules.ts`).
- **End to end.** The desktop-session harness ran against the `ee23b331` engine with this UI in a private sway session:
  - **Ctrl+S.** Hides the sidebar with the window intact. A pointer at the left edge peeks it; Ctrl+S restores it. The shipped UI logs `crashed TypeError: Kt.watchLeftEdge is not a function` and blanks the window.
  - **Splits.** F20D's tab-to-page split passes.
  - **Reorder.** Dropping a row in the top quarter of the row above reorders the sidebar.

F20D's reorder check drops 11.5 px into a 34 px row, inside the shared targeting's "drop onto" band (25–75 % of a row). By design that never reorders, on macOS either.
