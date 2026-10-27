# Lane U handoff (extensions): search providers, shortcuts, native messaging and the F09 flake

Issues: [#97](https://github.com/mckamyk/ardia/issues/97), [#98](https://github.com/mckamyk/ardia/issues/98) and [#100](https://github.com/mckamyk/ardia/issues/100) stay open for runtime acceptance; [#159](https://github.com/mckamyk/ardia/issues/159) tracks this source implementation; [#144](https://github.com/mckamyk/ardia/issues/144) is fixed. Branch `t3/linux-extension-followups`. The import half of the lane (#109, #110, #111) is a separate PR.

None of this ran inside Ardia: this lane can't build Chromium 156. Everything below is source-level, with compiled fixtures, stock-Chromium self-tests and patch dry runs against the 156 tree.

## What was done

### #144: the flaky F09 badge self-test

`Target.createTarget` returns before the new tab's navigation commits. Until then `chrome.tabs.query` reports that tab with `url: ""` and only a `pendingUrl`, so `tabs.find((t) => t.url.endsWith("#b"))` was `undefined` and `b.id` threw. A diagnostic run caught 3 of 8 launches in that state.

The test now waits (30 s deadline) until both tabs are `complete` with committed URLs before setting badges. Six parallel launches, three rounds: 2 of 18 failed before, 0 of 18 after.

### #97: extension search providers

Chromium builds and registers the settings-overrides API and manifest handler on Linux. The only gate is `chrome/common/extensions/api/_manifest_features.json`, which limits `chrome_settings_overrides` to `win` and `mac`.

`engine/patches/chromium-linux-settings-overrides.patch` (series, `linux`) adds `linux` to that list and changes nothing else. The existing `search.engines` → `extensionEnginesFromChrome` path from #93 then maps the provider into Ardia's extension search engines and drops it on uninstall. No Linux UI change was needed.

### #98: extension keyboard shortcuts

Before, Chromium's `ExtensionKeybindingRegistryViews` belonged to the hidden BrowserView toolbar. `_execute_action` either did nothing or tried Chromium's invisible popup, and named commands were never checked against Ardia's shortcut table.

- **`ac_linux_extension_shortcuts.{h,cc}`** (std only, fixture-tested). `ShortcutBook` keeps the registered keys and decides each key press:
  - if handling is suspended, or Ardia binds the key, it returns `kNone`;
  - `_execute_action`, `_execute_browser_action` and `_execute_page_action` return `kAction`, and a held (repeat) key returns `kSwallow`;
  - any other command returns `kCommand`.
- **`ac_linux_extension_keybindings.{h,cc}`** is `LinuxExtensionKeybindings`, an `ExtensionKeybindingRegistry` like macOS's `ExtensionShortcuts`:
  - it populates the regular named commands and the action command (`ShouldIgnoreCommand` returns false);
  - `kCommand` runs `NotifyEventTargets`, which fires `chrome.commands.onCommand` with the active tab;
  - `kAction` hands the active tab to the host.
- **`ac_linux_host.cc`** hunks:
  - `OnKeyEvent` runs Ardia's `AcceleratorPressed` first, then `ExtensionShortcutPressed(*event, shortcuts_.contains(accelerator))`. A key in Ardia's table never reaches an extension, even when Ardia ignores it (`notInText` in a text field).
  - The registry is created lazily for windows that listen for extensions, and never for an off-the-record page profile (#99). It is reset in `UnregisterAccelerators`, which runs on detach, window destruction and in the destructor.
  - The action shortcut emits `extensions.actionShortcut {tabId, extensionId}` through the window's `ExtensionEvent`. The shared bridge already turns that into `openExtensionFromMenu`, so the popup or side panel opens under Ardia's own toolbar button.
  - Extension lookups use the layout key code (`event.key_code()`), as Chromium does. Ardia's own table keeps its physical-key accelerator.
- **Not done.** Chromium 156 has no `_execute_side_panel` command; a manifest that declares it gets an ordinary named command. The action shortcut toggles a side panel when the extension set `openPanelOnActionClick`. Global commands keep Chromium's own listener.

### #100: native messaging hosts

Helium's `scan-chrome-native-messaging-hosts.patch` (applied on Linux; not in `03-helium.py`'s skip list) already looks in `~/.config/google-chrome/NativeMessagingHosts` and `/etc/opt/chrome/native-messaging-hosts`, after Ardia's own folder and `/etc/chromium/native-messaging-hosts`. Only Chromium's user folder was missing.

- **`engine/patches/chromium-linux-native-messaging-hosts.patch`** (series, `linux`) adds `g_ardia_native_messaging_directories` to `launch_context_posix.cc`. It is consulted for user-level hosts after Chrome's user folder and before the system folders.
- **`ac_linux_native_messaging.{h,cc}`** (std only):
  - `ExtraUserDirectories` returns `<config>/chromium/NativeMessagingHosts`, where `<config>` is `CHROME_CONFIG_HOME`, `XDG_CONFIG_HOME` or `~/.config`, the first that is absolute, as Chromium resolves it;
  - `ValidHostName` mirrors `NativeMessagingHostManifest::IsValidName`;
  - `FindManifest` takes only regular files, from absolute folders.
- **`ac_linux_host.cc`.** `StartLinuxHost` installs the hook (`NativeMessagingDirectories`, reading the environment at call time).
- **Docs.** The full lookup order is in `docs/linux-port.md` › "Extensions: search providers, shortcuts and native messaging".

## Contract

| Piece | Contract |
| --- | --- |
| event `extensions.actionShortcut` | `{tabId, extensionId}` on module `ArdiaExtensions`, event `extensions`, the same envelope as `extensions.actionPopup`; only to the window whose key it was |
| Ardia shortcut precedence | a key in the window's `ArdiaShell.setKeyboardShortcuts` table is never routed to an extension |
| Native messaging lookup | Ardia data dir, Chrome user dir, Chromium user dir (new), `/etc/chromium`, `/etc/opt/chrome` |
| `chrome_settings_overrides` | allowed on Linux; `search.engines` reports providers with `isOmniboxExtension: false` |

## Tests run

| Command | Result |
| --- | --- |
| `python3 -m unittest test_extension_followups` | 7 passed. They cover the compiled `ShortcutBook` and native-messaging helper under ASan and UBSan, both patches applying with `-F0` to the Chromium 156 files, and the wiring: Ardia first, private refusal, lifetime, the hook declaration matching the host's `extern`, GN, and no comments. All 7 fail on `linux`, where the files and hunks don't exist. |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | See the PR description for the final count. |
| `pnpm --filter @ardia/linux acceptance:self-test` | 68 passed. The new `extension-followups.test.mjs` adds 5. A stock Chromium round-trips `sendNativeMessage` through the generated echo host, refuses a host that doesn't list the extension, reports an unknown host as not found, and assigns both suggested shortcuts. |
| The #144 reproduction (18 parallel runs of the badge test) | 2 failures before, 0 after |

The C++ glue (`ac_linux_extension_keybindings.cc`) and the host hunks were checked by hand against the 156 headers (`ExtensionKeybindingRegistry`, `CommandService::GetNamedCommands`/`GetExtensionActionCommand`, `TabListInterface::From(BrowserWindowInterface*)`). They weren't compiled: this lane doesn't build Chromium.

## Acceptance scenarios

`apps/linux/scripts/acceptance/scenarios/e-extension-followups.mjs` adds three scenarios, each loading `fixtures/extension-followups` with `--load-extension`:

- **E97 (#97).** The provider is listed by `search.engines`, mapped into Ardia's engines and removed on uninstall.
- **E98 (#98).** Chromium assigns both shortcuts. The key presses themselves are manual: CDP key events skip the native window handler.
- **E100 (#100).**
  - Echo hosts answer from Chromium's and Chrome's user folders.
  - Ardia's own folder wins over both.
  - A host that doesn't list the extension is forbidden, and an unknown host isn't found.

Rows and manual steps are in `acceptance-suite.md`.

## Still needs native runtime acceptance on a Chromium 156 build

- Both patches apply in the real series and the new sources compile; `gn check` passes with `//chrome/browser/tab_list`.
- `pnpm --filter @ardia/linux acceptance -- <ardia> apps/linux/dist --only E97,E98,E100`.
- The manual shortcut steps (real keyboard input), and a real password manager connecting.

## Follow-ups

- #99 (existing): extensions, and so their shortcuts, in private windows.
