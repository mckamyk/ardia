# Lane J handoff: passwords and autofill management (#17 F07)

Branch `t3/linux-passwords-autofill`, based on `linux` at `2376f370`. This lane is the source implementation for #17, tracked by #96. Runtime acceptance stays with #17 and the #81 suite.

The path every secret takes, for the reviewer, is in [f07-security-notes.md](f07-security-notes.md).

## What was done

- **Linux OS reauthentication** (`engine/chromium/.../ac_reauth_linux.cc`, with its decision logic in `ac_reauth_linux_policy.h`)
  - **Before.** It always refused.
  - **Now.** It asks polkit (`CheckAuthorization`, `system-bus-name` subject, action `dev.ardia.linux.reauthenticate`, user interaction allowed, 120 s timeout, cancellable).
  - **Grant window.** It keeps Chrome's grant-validity window per profile.
  - **Refusals.** It supersedes older prompts and cancels the agent prompt on timeout. It grants only an authorized, non-challenge reply. Background instances and off-the-record profiles are refused without a prompt. There is no test grant.
  - **Shared code.**
    - `BUILD.gn` adds `//dbus` and `//components/dbus/thread_linux` to the Linux block only.
    - `ac_autofill_card_number` no longer hard-fails on Linux. It uses the same reauthentication with a Linux-only purpose string; the macOS string and flow are unchanged.
- **Credential helper** (`engine/ardiacore-linux/src/ardia/linux/ac_linux_credentials.{h,cc}`, std-only and fixture-tested). It plans every call:
  - validation and size limits;
  - origin normalization: http(s) is lowercased with userinfo dropped; other schemes such as `android://hash@pkg` stay verbatim;
  - exact JSON arguments, where secrets are written only as `<key>Hex` into a buffer that is measured first and wiped;
  - which stores must be available, preference reads and writes, and reply shapes;
  - engine-error classification against fixed constant messages;
  - "unavailable" assessment;
  - export cleanup.
- **Chromium binding** (`ac_linux_credential_service.{h,cc}`)
  - **Arguments.** It converts WebUI values to wiping `Arg`s.
  - **Profile.** It resolves the profile through `CredentialProfile()`.
  - **Store probe.** It checks the password store's `GetError()`, the `PersonalDataManager` and the OSCrypt encryptor (10 s deadline).
  - **Calls.** It runs preference reads and writes and calls `ac_engine_call`. It extracts only the secret field from secret replies and `Cleanse`s the parse.
  - **Export.** It shows the save dialog and cleans up a failed export.
- **Host registration** (`ac_linux_host.cc`, four narrow hunks).
  - Generic `passwords.*`/`autofill.*` names go to `LinuxCredentialsCall` before the route table; private windows get `ERR_PROFILE_PRIVATE`.
  - Typed `ArdiaPasswords.*`/`ArdiaAutofill.*` methods go to `LinuxCredentialsInvoke`.
  - The nine typed methods are advertised in bootstrap `capabilities`.
- **Bootstrap.** `bootstrap.js` gives interactive credential invokes longer deadlines (180 s; export 900 s). Before, a polkit prompt or the save dialog was cut off at the 30 s request timeout.
- **JS adapter** (`apps/linux/src/hostCredentials.ts`, one registration hunk in `hostModules.ts`)
  - **Secret verbs.** `ArdiaPasswords.call("unlock"|"reveal"|"export")` and `ArdiaAutofill.call("cardNumber")` become typed invokes.
  - **Secret writes.** `save`/`update`/`saveCard` become typed invokes too.
  - **Generic refusal.** Generic secret verbs are refused locally.
  - **List shapes.** List replies get macOS's `created`/`modified` shape.
  - **Status.** `passwordStatus`/`autofillStatus` and `credentialNotice` back the UI notice.
- **UI honesty** (Vite replacements, shared files untouched)
  - **Pane data.** `credentialPaneData.ts` re-exports the shared `paneData` but replaces `passwordAutofill`, `addresses`, `cards` and `autofillEnabled`, so a failure stays unloaded (`undefined`) instead of becoming `true`/`[]`. It maps `paneNeeds`/`usePaneLoaded`/`preparePane` onto them.
  - **Panes.** `LinuxPasswordsPane.tsx`/`LinuxAutofillPane.tsx` wrap the shared panes with `CredentialNotice`. The notice names a locked keyring, missing encryption, unavailable system authentication or the basic store.
- **Packaging.** `scripts/linux/dev.ardia.linux.policy` is copied by `scripts/linux/package.py` to `share/polkit-1/actions/`.
- **Acceptance.** `apps/linux/scripts/acceptance/scenarios/f07-passwords-autofill.mjs` (F07, #17) is registered in the catalogue, with its rows in [acceptance-suite.md](acceptance-suite.md).

## Contract

### Typed methods (`host.invoke`, positional `args`, profile first)

| Method | Args after profile | Reply | Gate |
| --- | --- | --- | --- |
| `ArdiaPasswords.status` | — | `{passwords: Availability, reauth, backend}` | none |
| `ArdiaPasswords.unlock` | — | `{unlocked}` | store; polkit when entries exist |
| `ArdiaPasswords.reveal` | origin, username | `{password: string \| null}` | store; polkit (5 min window) |
| `ArdiaPasswords.save` | origin, username, password | `{ok: true}` | store |
| `ArdiaPasswords.update` | origin, username, newUsername \| null, newPassword \| null | `{ok: true}` | store; polkit (5 min window) |
| `ArdiaPasswords.export` | — | `{status, path?}` | store; `ReauthAvailable()` before the dialog; polkit every time |
| `ArdiaAutofill.status` | — | `{addresses, cards: Availability, reauth, backend}` | none |
| `ArdiaAutofill.saveCard` | card `{id?, name?, expMonth?, expYear?}`, number \| null | `{id}` | cards available |
| `ArdiaAutofill.cardNumber` | id | `{number: string \| null, authenticated}` | cards available; polkit every time |

`Availability` is `{available, reason?, code?, message?}`. The reasons are:

- `privateProfile`;
- `noStore`;
- `keyringLocked`;
- `storeFailed`;
- `encryptionUnavailable`.

The profile must be `""` or `"default"`.

### Generic domain calls (`engine.call`)

| Call | Service or preference |
| --- | --- |
| `passwords.list` | `ac_passwords_list` |
| `passwords.remove` | `ac_passwords_remove` |
| `passwords.neverSaveOrigins` | `ac_passwords_exceptions` |
| `passwords.allowSaving` | `ac_passwords_allow` |
| `passwords.autofillEnabled` / `setAutofillEnabled` | the `credentials_enable_service` preference |
| `autofill.settings` / `setSettings` | `autofill.profile_enabled` / `autofill.credit_card_enabled` |
| `autofill.addresses` | `ac_autofill_addresses` |
| `autofill.saveAddress` | `ac_autofill_save_address` |
| `autofill.cards` | `ac_autofill_cards` |
| `autofill.remove` | `ac_autofill_remove` |

- **Secret names.** `passwords.{unlock,reveal,export,save,update,add}` and `autofill.{cardNumber,saveCard}` are refused with `ERR_SECRET_ROUTE`.
- **Managed preferences.** Writes to a policy-managed preference are refused with `ERR_PREFERENCE_MANAGED`.

### Error codes

- **Availability:** `ERR_PROFILE_PRIVATE`, `ERR_PROFILE`, `ERR_PASSWORDS_UNAVAILABLE`, `ERR_AUTOFILL_UNAVAILABLE`.
- **Authentication:** `ERR_REAUTH_UNAVAILABLE`, `ERR_REAUTH_DENIED`.
- **Requests:** `ERR_SECRET_ROUTE`, `ERR_CREDENTIAL_REQUEST`, `ERR_CARD_NUMBER`, `ERR_EXPORT_PATH`.
- **Store and engine:** `ERR_NOT_FOUND`, `ERR_PASSWORD_REJECTED`, `ERR_STORE_WRITE`, `ERR_ENGINE_CLOSED`, `ERR_BAD_REPLY`, `ERR_PREFERENCE`, `ERR_PREFERENCE_MANAGED`, `ERR_ENGINE`.

### Profile resolution

All credential work goes through `CredentialProfile()` in `ac_linux_credential_service.cc`:

- today it accepts the window's regular (default) profile and refuses off-the-record windows with `ERR_PROFILE_PRIVATE`;
- when #62's profile registry lands, re-point that one function;
- the generic-route private check in `ac_linux_host.cc` must move with it.

## Tests run

All on this machine (Arch, clang 22, Node 26). The native ones compile real sources against stubs.

- **`python3 -m unittest discover -s engine/ardiacore-linux/tests`:** TOTAL_NATIVE. It includes the new `test_credentials.py`, whose 3 tests cover:
  - plans and exact payloads (hex-only secrets, whitelisted fields, stray secrets dropped);
  - the `ERR_SECRET_ROUTE` refusals;
  - the availability matrix;
  - error classification that never echoes input;
  - wipe-on-clear and wipe-on-move;
  - export cleanup in a scratch directory, including a symlink and a pre-existing file;
  - native/TS capability parity;
  - a source check that there are no log, trace or crash-key calls.
- **`node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`:** 8/8. This includes the new per-method deadline test.
- **`engine/chromium/src/chrome/browser/ardia/tests`:** new `test_reauth_linux.py`, 3/3. It compiles the real `ac_reauth_linux.cc` against a fake D-Bus and covers:
  - background and off-the-record refusals without D-Bus;
  - the exact `CheckAuthorization` message;
  - the grant window: covered, zero validity, expiry, and no caching after a refusal;
  - ten refusal shapes (denied, dismissed, challenge, authorized-with-challenge, cancelled, unregistered action, no polkit, no reply, malformed reply, malformed details);
  - timeout plus `CancelCheckAuthorization`;
  - superseding, including a stale grant reply that is ignored;
  - superseding before the bus name;
  - a re-entrant request from a superseded callback;
  - an unconnected bus;
  - profile teardown;
  - wrong-thread abort;
  - policy-file/action-id parity.

  A mutation check (grant on `authorized || !challenge`) fails it.

  The old `test_real_linux_reauth_never_grants` is replaced by this file. `test_linux_port.py` was already failing on `linux` before this lane, on a stale check that forbids `ardia/core/` includes and `//chrome/browser/device_reauth` outside `is_mac` (`ac_downloads.cc` hits it); it still has that one failure.
- **`pnpm --filter @ardia/linux typecheck`:** clean.
- **`pnpm --filter @ardia/linux test:host`:** 241/241, including the new `hostCredentials.test.mjs` (9 tests). Those tests run the real `bootstrap.js` in a VM and drive the shared `packages/ardiacore` `passwords.ts`/`autofill.ts` end to end. They cover:
  - generic versus typed wire traffic, with no secret in any `engine.call` payload;
  - local refusals that send nothing;
  - denied, cancelled and expired outcomes that never reveal, change or export;
  - unavailable versus empty;
  - the notice text;
  - console capture with no secret.
- **`pnpm --filter @ardia/linux build`:** OK. The bundle contains the pane adapters.
- **`pnpm --filter @ardia/linux test:graph`:** 1/1.
- **`pnpm --filter @ardia/linux acceptance:self-test`:** 46/46, with the catalogue now including #17.
- **`engine/patches/build/tests/test_platform_tooling.py`:** 17/17, including the policy in the package.
- **Syntax check against Chromium 156.** `clang++ -fsyntax-only -Werror` ran with the real 156 headers and the `ardia/linux` and `chrome/browser/ardia` target flags read from the existing out-dir `.ninja` files (nothing built or written there). It passes for:
  - `ac_linux_credentials.cc`;
  - `ac_linux_credential_service.cc`;
  - `ac_reauth_linux.cc`;
  - `ac_autofill.cc`.

  `ac_linux_host.cc` couldn't be checked that way, because that out dir lacks a generated mojom header; its hunks mirror the downloads registration.

## Needs native runtime acceptance on a Chromium 156 build

I couldn't build or run Chromium 156 here, so none of this has run as a browser.

- **Automated.** Run `pnpm --filter @ardia/linux acceptance -- <ardia> apps/linux/dist --only F07`. It covers CRUD, refusals, toggles, restart, encryption at rest and logs with `--password-store=basic`.
- **Manual.** These are listed in the scenario and in [acceptance-suite.md](acceptance-suite.md):
  - polkit grant, deny, dismiss and timeout with the policy installed and an agent running;
  - the five-minute window;
  - the export dialog and the CSV mode `0600`;
  - a locked Secret Service;
  - private windows after #62.
- **First real build.** Confirm that the new GN deps resolve and that `SelectFileDialog` uses the portal or GTK under Ardia's host. Confirm too that polkit accepts the `system-bus-name` subject from the shared system bus.

## Follow-ups opened

- #102: report a silent OSCrypt fallback from the keyring to the v10 key.
- #103: cleanse secret-bearing engine reply buffers (shared engine).
- #104: the shared panes' macOS wording (Keychain, Touch ID) on Linux.
- #105: install the polkit policy from distribution packages.

Two related gaps aren't filed:

- macOS's `OriginOf` (in `ArdiaServices.mm`) strips the userinfo hash from `android://` realms. Linux now keeps it. This is a macOS issue, outside this lane.
- Native Chromium save and fill prompts (`autofill.showSuggestions`) are out of #17's scope, as the issue states.
