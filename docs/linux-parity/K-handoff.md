# Lane K handoff: extensions management and toolbar (#19 F09)

Branch `t3/linux-extensions`, rebased onto `linux` at `4fd0946e` (after #62, #63 and #68). This is source-level work, tested with compiled fixtures, Linux host tests and the acceptance harness's self-test. I couldn't build it into Chromium 156 or run it natively; a full Chromium build was running on this machine, and its generated extension mojom headers didn't exist yet.

## What was done

The shared UI already speaks the typed `extensions.*` domain. Linux routed most management calls straight to the shared `ac_extensions_*` engine services but returned raw `ExtensionInfo`, so:

- the toolbar had no `hasAction`, `popup`, `actionIcon` or `sidePanel`;
- nothing emitted `extensions.changed`;
- `actionStates`, `sidePanelUrl`, `inspectUnpacked`, `chooseFolder` and `resolveInstallPrompt` had no handler;
- `executeExtensionAction` failed as an unknown tab command;
- popups and side panels opened as ordinary temporary tabs instead of extension views;
- install and re-enable prompts never reached Ardia's dialog.

### Native (engine/ardiacore-linux)

**`ac_linux_extensions.{h,cc}`** is pure logic with no Chromium dependency beyond `base/values.h`:

- **`ValidateRequest`** gives every `extensions.*` call an exact shape:
  - ids are 32 `a`–`p` characters;
  - paths are absolute and normalized;
  - `actionStates` takes at most 64 ids;
  - `resolveInstallPrompt` takes `{requestId ≤ 64, accepted: bool}`;
  - `configure` accepts only `pinned`, `fileAccess`, `incognito` and `siteAccess`;
  - any other `extensions.*` name is refused (`ERR_EXTENSION_REQUEST`), so nothing is forwarded blindly. `syncApply` passes through unchanged, as before; it belongs to sync (F13).
- **`Configuration`** replaces the host's old `ExtensionConfiguration` and now also checks the id.
- **`ReadManifest`, `IconPath`, `DecorateEntry`, `Package`** reproduce macOS's `ArdiaServices`/`ReadManifest` listing semantics:
  - `hasAction`, `popup`, `actionTitle`, `actionIcon` (a data URL), `sidePanel`, `incognitoSplit`, `fromWebStore` and `webStoreUrl`;
  - the nullable fields are always present.
- **`SearchEngines`** rebuilds the `search.engines` reply with typed fields only. An extension is attached only when its id is valid.
- **`ChangeFor` / `ChangeEvent`** map successful calls to `extensions.changed` (`installed`, `uninstalled`, `enabled`, `disabled`, `reloaded`, `configured`).
- **`ActionState`** formats per-tab action state: a badge starting with `<<` is hidden, and a transparent badge colour becomes `null`.
- **`ValidateSurfaceRequest`, `SurfaceBook`, `PromptLedger`, `PendingPopup`** hold the bookkeeping:
  - at most 4 extension views per window;
  - view ids are never reused, and every view is tagged with its renderer generation;
  - at most 8 pending install prompts;
  - `chrome.action.openPopup` waits at most 10 s.

**`ac_linux_extension_service.{h,cc}`** contains:

- **`LinuxExtensionsCall`**, which owns every `extensions.*` call and `search.engines`. It parses and validates the request, then either answers natively or forwards it to the engine route named in `ac_linux_contract.h`:
  - `list` is decorated from the loaded `Extension` manifest; action icons are read on a blocking pool, at most 512 KB, only from inside the extension's folder.
  - `inspectUnpacked` runs Chromium's own `file_util::LoadExtension` off the UI thread and returns an `ExtensionPackage`. A failure is `ERR_EXTENSION_INVALID`.
  - `chooseFolder` uses `ui::SelectFileDialog` (`SELECT_FOLDER`) parented to the window. Background instances log and return `null`.
  - `actionStates` and `sidePanelUrl` use the window's own tab binding (`ERR_TAB_UNAVAILABLE` otherwise), `ExtensionActionManager` and `SidePanelService`.
  - `resolveInstallPrompt` resolves only from the window the prompt was shown in (`ERR_EXTENSION_PROMPT_STALE` otherwise).
  - After a successful management call, `extensions.changed` goes to every window of the profile, including Settings.
- **`LinuxExecuteExtensionAction`** runs `ExtensionActionRunner::RunAction(extension, true)` and returns `popup`, `sidePanel` or `none`.
- **Hooks:**
  - `g_ardia_extension_install_prompt` sends Chromium's `InstallPromptData` (type, permissions, icon) to the shell as `extensions.installPrompt`, aimed at the window that owns the parent tab, or else the profile's active window. Nothing is ever auto-accepted. If no window can show the prompt, the done callback is handed back and Chromium shows its own dialog. External-install prompts are aborted, as on macOS. When the target window or its renderer goes away, the prompt is re-sent to another window of the profile, or cancelled (`USER_CANCELED`).
  - `g_ardia_extension_side_panel` sends `extensions.sidePanel {tabId, extensionId, open}`.
  - `g_ardia_open_action_popup` (`chrome.action.openPopup`) sends `extensions.actionPopup` and remembers Chromium's callback until a popup for that extension opens in that window. It runs on load, or with `null` on failure, close or the 10 s timeout.
- **Profile resolution** goes through #62's registry, in two small functions:
  - `LinuxExtensionsProfile` serves calls. It parses the requested profile name and resolves it asynchronously with `ResolveLinuxProfile`, so the default and isolated profiles both work. Private names or off-the-record results get `ERR_PROFILE_PRIVATE`; other failures get the registry's code.
  - `LinuxExtensionViewProfile` serves extension views. It uses the window's page profile and requires the requested name to be the one that window hosts (`ERR_PROFILE_WINDOW` otherwise). Private windows get `ERR_PROFILE_PRIVATE`.
  - `actionStates`, `sidePanelUrl` and `resolveInstallPrompt` don't take a profile from the shared UI. They use the calling window's page profile, so isolated-profile windows can answer their own prompts. `actionStates` and `sidePanelUrl` also require the tab to belong to that profile.
  - Events and prompts carry the registry's profile name. `extensions.changed` goes only to windows of the changed profile.
  - Extensions in private windows remain #99.

**`ac_linux_extension_views.{h,cc}`** contains `LinuxExtensionSurfaces`, one per window. It hosts popups and side panels as real `ExtensionViewHost`s, made by `ExtensionViewHostFactory::CreatePopupHost` and `CreateSidePanelHost`, in a `views::WebView` child of the window's host view:

- The contents are attached while hidden, so auto-resize works before the page shows. Popups auto-resize between 25×25 and 800×600 and report `preferredSize`.
- A placement brings the view to the front. It scales by the shell zoom and clamps to the window.
- `focus` focuses the page. `evaluate` returns `null`, and any other command is refused.
- The view is closed, and `windowClose` reported to the shell, when:
  - the page calls `window.close()` or presses Escape (Chromium's close handler);
  - the extension unloads;
  - its renderer dies.
- Views from an older shell renderer are destroyed silently. All of a window's views are destroyed when the window closes or detaches.

**`ac_linux_host.cc`** changes are narrow hunks:

- includes;
- `engine.call` hands `extensions.*` and `search.engines` to `LinuxExtensionsCall` before the generic default-profile gate and the generic route;
- the old `extensions.configure` block, `ExtensionConfiguration` and the `search.engines` reshaping are removed;
- `tab.command executeExtensionAction`;
- `host.invoke extensionView.*` with the window's renderer generation and shell zoom;
- the `extensionView.*` capabilities in the bootstrap;
- `RetireRenderer`, the destructor and `DetachBrowser` release views and re-send or cancel prompts;
- `StartLinuxHost` installs the delegate (deliver, window choice, tab key, views, broadcast).

`BUILD.gn` lists the new files and adds `//chrome/browser/extensions`, `//chrome/browser/ui/extensions:extension_popup_types`, `//extensions/common` and `//ui/shell_dialogs`.

**`engine/patches/chromium-linux-action-popup-hook.patch`**, registered `linux` in `series`, defines `g_ardia_open_action_popup` in `extension_action_api.cc`. On macOS the same hook comes from `engine/ardiacore/apply.sh`. It applies with zero fuzz to the 156 source.

### Linux UI (apps/linux)

- **`hostExtensionView.ts`** (`HostExtensionView`):
  - creates a native extension view with `{kind, url, profile}`;
  - buffers early events;
  - serializes and deduplicates viewports;
  - releases the view when the WebView unmounts, including while it is still being created;
  - stops listening if creation is refused.
- **`shims/web/HostExtensionWebView.tsx`** maps the events to the shared WebView props: `ready`, `navigation`, `preferredSize` → `onPreferredSize`, `windowClose` → `onWindowClose`. Its handle supports `focus` and `evaluate`.
- **`HostWebView.tsx`** sends `extensionHost` WebViews (the shared `ActionPopup` and `SidePanel`) to it when the host advertises `extensionView.create`; otherwise it falls back to the old temporary tab.
- **`main.tsx`** exposes `ardiaLinux.extensions` (the shared store) and `ardiaLinux.openExtension` (the shared toolbar activation) for the acceptance suite.

## Contract

| Call / event | Arguments | Reply |
| --- | --- | --- |
| `extensions.list` | `{}` | `{extensions: InstalledExtension[]}`, decorated |
| `extensions.install` / `installCrx` / `inspectUnpacked` | `{path}` (absolute, normalized) | `{id}` / `{id}` / `ExtensionPackage` |
| `extensions.setEnabled` | `{id, enabled}` | `{}`; re-enabling with more permissions prompts first |
| `extensions.uninstall` / `reload` | `{id}` | `{}` |
| `extensions.configure` | `{id, options}` | `{}` |
| `extensions.actionStates` | `{tabId, ids ≤ 64}` | `Record<id, ActionState>` for that tab |
| `extensions.sidePanelUrl` | `{tabId, extensionId}` | URL or `null` |
| `extensions.chooseFolder` | `{}` | path or `null` |
| `extensions.resolveInstallPrompt` | `{requestId, accepted}` | `null` |
| `search.engines` | `{}` | `{list: {engines}}` |
| event `extensions.changed` | `{profile, id, event}` (registry name; `""` for the default profile) | |
| event `extensions.installPrompt` | `{requestId, profile, id, name, version, type, icon, permissions, tabId}` | |
| event `extensions.sidePanel` / `extensions.actionPopup` | `{tabId, extensionId[, open]}` | |
| `tab.command executeExtensionAction` | `[id]` | `"popup"`, `"sidePanel"` or `"none"` |
| `extensionView.create` | `{kind: popup or sidePanel, url: chrome-extension://…, profile}` | `{id}` |
| `extensionView.viewport` / `command` / `release` | `{id, rect, visible}` / `{id, command, args}` / `{id}` | `null` |
| events `ArdiaExtensionView`/`<id>` | `ready`, `navigation`, `preferredSize`, `windowClose` | |

All errors are `{code, message}`: `ERR_EXTENSION_REQUEST`, `ERR_EXTENSION_INVALID`, `ERR_TAB_UNAVAILABLE`, `ERR_EXTENSION_PROMPT_STALE`, `ERR_PROFILE_INVALID`, `ERR_PROFILE_WINDOW`, `ERR_PROFILE_PRIVATE` and the registry's other `ERR_PROFILE_*` codes, `ERR_EXTENSION_UNAVAILABLE`, `ERR_EXTENSION_VIEW_REQUEST`, `ERR_EXTENSION_VIEW_LIMIT`, `ERR_EXTENSION_VIEW_STALE`, `ERR_UNKNOWN_COMMAND`, `ERR_ENGINE`.

## Tests run

- `pnpm --filter @ardia/linux typecheck`: passes.
- `pnpm --filter @ardia/linux test:host`: 241 pass, 0 fail. The new `src/hostExtensions.test.mjs` adds 9 tests:
  - capability fallback;
  - create arguments;
  - early and foreign events;
  - closing during creation;
  - viewport serialization;
  - stale views;
  - refused creation;
  - domain routing;
  - `executeExtensionAction` as a tab command.
- `pnpm --filter @ardia/linux build`, `test:graph` (1) and `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` (7): pass.
- `python3 -m unittest test_extensions test_extension_contract`: 9 pass. Each compiled fixture runs under ASan and UBSan.
  - `extensions_scenarios.cc` covers the pure helper.
  - `extension_views_scenarios.cc` compiles the real `ac_linux_extension_views.cc` against stubs. It covers:
    - open, place, focus and release;
    - rejections, including private, a profile the window doesn't host, disabled extension, the limit and an unhostable page;
    - closes by `window.close()`, crash and unload (only the unloaded extension's views close);
    - a release racing a close;
    - auto-resize for popups only;
    - renderer retirement and window close with no stray events;
    - `openPopup` callbacks: matched, missing document, closed first, superseded, expired, window closed.
  - Wiring checks: routes, claim order in `engine.call`, retirement hunks, hooks installed before the event sink, prompts never auto-accepted, GN, and the patch applying to the 156 source.
- Full `python3 -m unittest discover -s engine/ardiacore-linux/tests`: see the PR description for the final count.
- `pnpm --filter @ardia/linux acceptance:self-test`: 51 pass. It now includes an F09 fixture suite and, against the stock Chromium 155, a run where the fixture loads with `--load-extension`, a badge set for one tab stays on that tab, and the side-panel behaviour is set.
- A syntax-only compile of `ac_linux_extensions.cc` with the 156 toolchain flags and real `base` is clean. `ac_linux_extension_views.cc` and `ac_linux_extension_service.cc` couldn't be syntax-checked: the 156 output directory doesn't have `extensions/common/mojom/*.mojom.h` yet. I checked their Chromium API use by hand against the 156 headers.

The new tests fail against `linux`: the helper, views and test files don't exist there, the host has no `extensionView.*` or `executeExtensionAction`, and `extensions.list` returned undecorated entries.

## Acceptance scenario

`apps/linux/scripts/acceptance/scenarios/f09-extensions.mjs` (#19) loads `scripts/acceptance/fixtures/extension` with `--load-extension`. That fixture is an MV3 extension with an action popup, a side panel, an omnibox keyword and a reporting service worker. The scenario checks:

1. the listing fields;
2. refusal of unknown and malformed calls;
3. the omnibox keyword is in `search.engines` but not in Ardia's search engines;
4. turning it off and on, with events;
5. reload picks up the edited version;
6. a badge appears only on the tab it was set for;
7. toolbar activation opens a real popup (`getViews({type: "popup"})` sees it) that closes, with its host destroyed, on a tab switch;
8. `openPanelOnActionClick` opens the side panel;
9. Load unpacked's inspect and install, with the event;
10. uninstall removes the extension, closes its side panel and drops its keyword.

The harness used to launch every browser with `--disable-extensions`. That flag is now opt-out (`extensions: true`), and only F09 opts out.

Manual: Web Store consent and Cancel, the permission-increase re-enable prompt (needs two signed CRXs), Load Unpacked through the real folder picker, and popup placement, focus and keyboard.

## Still needs native runtime acceptance on a Chromium 156 build

- The new sources and the `linux` patch compile and link, and `gn check` passes for the new deps.
- The F09 scenario passes end to end. In particular:
  - `ExtensionViewHost` popups and side panels render inside the hosted window;
  - auto-resize reports a size while the view is hidden;
  - the native view sits above the tab viewports.
- Visual check (the screen may be locked):
  - the popup's native view is rectangular, so its corners need checking against the shared rounded `Surface`;
  - clicking a native page while a popup is open should close the popup.
- Web Store install shows Ardia's dialog with Chromium's permission text; Cancel installs nothing; re-enable after a permission increase asks first.
- `chrome.action.openPopup()` from an extension opens Ardia's popup and resolves.
- The folder picker opens through the portal or GTK.

## Open risks and follow-ups

- Prompts and side-panel events count as delivered when the window's renderer is attached, even if its extension listener hasn't subscribed yet. A prompt that arrives during the first moments of startup could wait until the window closes or its renderer reloads, and is then re-sent or cancelled.
- `evaluate` is not supported in extension views. The shared popup sizes itself from `preferredSize`; its `evaluate` fallback gets `null`.
- Follow-ups:
  - #97: extension search providers (`chrome_settings_overrides`) are Windows- and macOS-only in Chromium;
  - #98: extension keyboard shortcuts;
  - #99: extensions in private windows (isolated profiles already work through the registry);
  - #100: native messaging hosts;
  - #137: an install prompt outlives its parent tab (Chromium's own dialog aborts in that case).
- `chrome.action.openPopup` passes a null `ShowPopupCallback`. Expecting, expiring and failing a pending popup skip null callbacks, as macOS does.
