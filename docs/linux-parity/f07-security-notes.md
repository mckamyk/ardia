# F07 security notes: every path a secret takes on Linux

This covers saved passwords and full card numbers in the Linux port (#17, source work in #96). Non-secret data (origins, usernames, address fields, card network, last four digits, expiry, toggles) is described only where it shares a path with a secret.

## Storage

- **No Ardia copy.** Ardia stores no secret of its own. Passwords live in Chromium's `Login Data` and card numbers in `Web Data`, both encrypted by OSCrypt (os_crypt_async).
- **Encryption key.** It comes from the Secret Service (libsecret), KWallet or the secret portal, chosen by `--password-store`. With `--password-store=basic`, or when no keyring answers, Chromium falls back to its v10 key, which only obfuscates. Ardia reports the configured backend (`backend` in `ArdiaPasswords.status` / `ArdiaAutofill.status`). For `basic`, the Settings pane says so in plain words.
- **Silent fallback.** The silent fallback to v10 when a keyring is configured but fails is Chromium behaviour. Ardia can't yet tell it apart from real keyring encryption: follow-up #102.
- **Unavailable store.** If the password store reports `kKeychainError`/`kInactionable`, or the encryptor has no key, every password call fails with `ERR_PASSWORDS_UNAVAILABLE`. The card calls fail with `ERR_AUTOFILL_UNAVAILABLE`. None of them returns an empty list, so a locked keyring never looks like an empty vault.

## Authentication

- **Who asks.** `ardia::Reauth` on Linux (`engine/chromium/.../ac_reauth_linux.cc`) asks polkit over the system bus. It calls `CheckAuthorization` with:
  - the browser's own unique bus name as a `system-bus-name` subject, so polkit resolves the caller race-free;
  - action `dev.ardia.linux.reauthenticate`;
  - `AllowUserInteraction`;
  - a per-request cancellation id;
  - a 120 s timeout.
- **The policy.** `scripts/linux/dev.ardia.linux.policy` sets `auth_self` for active sessions only and `no` for inactive and remote sessions. It never uses `auth_self_keep`. The package ships it under `share/polkit-1/actions/`. It takes effect only once installed into `/usr/share/polkit-1/actions` (#105); until then polkit reports the action unknown and Ardia treats that as unavailable.
- **Only one reply grants.** The grant needs `is_authorized && !is_challenge` with no error.
- **Everything else refuses.**

  | polkit reply | Result |
  | --- | --- |
  | Not authorized | `denied` |
  | `polkit.dismissed` | `cancelled` |
  | `Error.Cancelled` | `cancelled` |
  | `NoReply`/`Timeout` | `expired`, and the prompt is cancelled |
  | A challenge, an unknown action, no polkit, no bus name, a malformed reply, no reply | `unavailable` |
  | An off-the-record profile, a background (`ARDIA_BACKGROUND`/`--ardia-background`) instance | `unavailable` without contacting polkit |

  There is no test grant on Linux. `ARDIA_TEST`/`ARDIA_INSTANCE` values are ignored.
- **Grant window.** It follows Chrome's DeviceAuthenticator semantics.
  - After a granted request with a positive validity, later requests on the same profile within that validity are granted without a prompt. That covers unlock, reveal and edit, at `kPasswordManagerAuthValidity` (5 minutes).
  - Export and card numbers pass zero validity, so they always prompt.
  - A refusal never extends the window.
- **Superseding.** A newer request cancels the older one: its callback gets `false` and its polkit check is cancelled. Late replies to old requests are ignored. Profile teardown cancels the pending check and drops its callback.
- **Events.** Each outcome emits `reauth.requested` with `purpose`, `granted` and `result`, and nothing else.
- **Availability probe.** `ProbeReauth()` (`ac_reauth_linux.h`) sends the same `CheckAuthorization` without user interaction and with a 5 s timeout, so it never prompts. It classifies the answer:

  | polkit answer | Reason |
  | --- | --- |
  | Authorized or challenge | `available` |
  | Not authorized without a challenge | `notPermitted` |
  | Unknown-action error | `policyMissing` |
  | No polkit or no bus name | `noPolkit` |
  | Background instance | `background` |
  | Anything else | `unavailable` |

  The status calls report it as `reauth`/`reauthReason`/`reauthMessage`, and the Settings notice explains it. Export runs the probe before showing the save dialog and refuses with `ERR_REAUTH_UNAVAILABLE` and that message.
- **What it gates.**
  - Engine side: `ac_passwords_unlock` (only when the vault has entries), `ac_passwords_reveal`, `ac_passwords_update` and `ac_passwords_export` gate on this. `ac_autofill_card_number` now does too on Linux; before, it was hard-disabled.
  - Linux side: before showing the export dialog, Linux runs the availability probe. It refuses with `ERR_REAUTH_UNAVAILABLE`, so no path is asked for when authentication can't happen.

## Paths, one by one

The UI renderer is the trusted `chrome://ardia/` WebUI. The host accepts `ardiaRequest` only from that committed frame (`Trusted()`), and ordinary pages have no `__ardiaHost`.

### Reveal a password (`getPassword` → `passwords.reveal`)

1. Shared `call("passwords.reveal")` reaches the Linux `ArdiaPasswords.call` adapter (`apps/linux/src/hostCredentials.ts`). It sends `host.invoke("ArdiaPasswords.reveal", [profile, origin, username])`, never `engine.call`.
2. The native typed handler (`LinuxCredentialsInvoke`):
   - refuses private profiles;
   - checks store availability;
   - builds `{"origin","username"}`;
   - calls `ac_passwords_reveal`, which asks for authentication.
3. The engine replies with JSON text. The handler parses it, copies only `password` (or `null`) into a fresh value, and `Cleanse`s the parsed tree.
4. The value goes back to the WebUI through `CallJavascriptFunction("__ardiaReply")`. The adapter turns it into `{"password":…}` text, because the shared `call()` contract parses JSON, and the pane shows it.

Without a grant the reply is `{"password": null}`, so the pane never shows a stale or partial value.

### Save a password (import, `savePassword`)

- **Route.** `ArdiaPasswords.save(profile, origin, username, password)` becomes `host.invoke("ArdiaPasswords.save", …)`.
- **Native.** The handler copies the arguments into wiping `Arg`s. It writes the password only as `passwordHex` into a buffer of exactly measured size, so the buffer never reallocates and leaves no copy behind. It calls `ac_passwords_add`, which decodes with `SensitiveArgs` and cleanses, then wipes the buffer.
- **No authentication.** Saving needs none, as on macOS and in Chrome.

### Change a password (`updatePassword`)

- **Route.** Same as saving, through `ArdiaPasswords.update`, with `newPasswordHex` and an optional `newUsername`.
- **Gate.** `ac_passwords_update` asks for authentication first. On refusal it returns `Not authenticated`, which maps to `ERR_REAUTH_DENIED`, and nothing changes.

### Export (`exportPasswords`)

1. `host.invoke("ArdiaPasswords.export", [profile])`. The renderer can't supply a path.
2. Native checks availability and runs the reauthentication probe. It then shows Chromium's `SelectFileDialog` (save-as, `.csv`), honouring the `AllowFileSelectionDialogs` policy through `ChromeSelectFilePolicy`. Cancel returns `{status: "cancelled"}`.
3. It records whether the file already existed, then calls `ac_passwords_export {path}`. That asks for authentication with zero validity, then Chromium's `PasswordManagerExporter` writes the CSV with mode `0600`.
4. **Cleanup.** On `writeFailed`, `cancelled` or `reauthFailed`, a file that didn't exist before the export and is now a regular file (never a symlink) is deleted. Chromium's exporter already deletes on write failure; this guards the rest. A file the user chose to overwrite is never deleted by Ardia.
5. The reply carries `status` and, only on success, `path`.

### Card numbers

- **Saving.** `ArdiaAutofill.saveCard(profile, card, number)` validates 12–19 digits for a new card. It sends `numberHex` to `ac_autofill_save_card`, the same way as a password.
- **Revealing.** `revealCardNumber` → `autofill.cardNumber` → the typed `ArdiaAutofill.cardNumber`. That calls `ac_autofill_card_number`, which always prompts, then extracts `number` and `authenticated` and cleanses the parse.
- **Listing.** Lists come from `ac_autofill_cards` and carry `last4` only.

### Unlock (`unlockPasswords`)

`ArdiaPasswords.unlock` carries no secret, but it is still typed:

- it needs authentication when the vault has entries;
- it must refuse with `ERR_PASSWORDS_UNAVAILABLE` rather than "unlock" an empty-looking vault when the store can't be read.

## Never on the generic route

- **Native refusal.** `engine.call` refuses with `ERR_SECRET_ROUTE` for: `passwords.{unlock,reveal,export,save,update,add}` and `autofill.{cardNumber,saveCard}`. The generic table (`linux_contract::kRoutes`) has no `passwords.*`/`autofill.*`/`ac_passwords*`/`ac_autofill*` entry at all.
- **Adapter refusal.** The JS adapter refuses generic `save`/`update`/`add`/`saveCard` before anything is sent.
- **Payload whitelist.** Generic payloads (`passwords.remove`, `passwords.allowSaving`, `autofill.saveAddress`, `autofill.remove`, …) are rebuilt from whitelisted fields, so a stray `password` or card number in the request is dropped. The fixture asserts this on the exact serialized text.

## Logs, telemetry, crash keys

- **Native.** `ac_linux_credentials*`, `ac_linux_credential_service*` and `ac_reauth_linux.cc` contain no `LOG`/`VLOG`/`printf`/trace/crash-key call; a test enforces this.
- **Engine errors.** Engine errors are passed to the UI only when they match a fixed list of constant messages. Anything else becomes "The engine refused the request", so an engine message can never echo input.
- **JS.** The adapter logs nothing. Shared panes log only `error.message` (`orWarn`), which comes from those constants. The host tests capture every console call during secret traffic and assert no fixture secret appears.
- **Acceptance.** The suite checks the browser log, `Login Data` and `Web Data` for the fixture password and card number after a restart.

## What isn't wiped, and why

- **Renderer.** V8 strings in the UI renderer (the revealed password shown in the sheet, the typed password in a text field) can't be wiped from JS.
- **Chromium's own buffers.** These aren't ours to wipe:
  - the WebUI IPC message carrying a typed request;
  - the `base::Value` passed to `CallJavascriptFunction`;
  - the engine's reply `std::string` (shared `ardia::Reply::Send`).

  The engine reply is the same on macOS. Wiping it through `c_str()` would be undefined behaviour, so it is left to follow-up #103 (a cleansing secret reply in the shared engine).

## Private windows and profiles

- **One resolver.** Credential calls resolve their profile through one function, `ResolveCredentialProfile()` in `ac_linux_credential_service.cc`, on #62's profile registry.
- **Private refusal.** A renderer whose window's page profile is off the record gets `ERR_PROFILE_PRIVATE` for every credential call, whatever profile it names. A private profile name (`incognito:…`) gets `ERR_PROFILE_PRIVATE` from any renderer. A name that resolves to an off-the-record profile is refused too. An OTR profile shares its parent's path, so letting it through would expose the regular vault.
- **Isolated profiles.** These resolve to their own `Profile <id>` directory, with their own `Login Data`, `Web Data` and preferences. The F07 scenario checks that a credential saved in one profile doesn't appear in the other.

## Not covered here

- Native Chromium save/fill prompts on web pages.
- Passkeys.
- 1Password and other native-messaging managers.
- Sync of secrets.
