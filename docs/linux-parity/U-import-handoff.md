# Lane U handoff (import): keyring and Firefox passwords, tabs, cookies, addresses and cards

Issues: [#109](https://github.com/mckamyk/ardia/issues/109), [#110](https://github.com/mckamyk/ardia/issues/110) and [#111](https://github.com/mckamyk/ardia/issues/111) stay open for runtime acceptance; [#158](https://github.com/mckamyk/ardia/issues/158) tracks this source implementation. Branch `t3/linux-import-extension-followups`. The extension half of the lane is in PR #166.

Nothing here ran inside Ardia: this lane can't build Chromium 156. The new native sources and the service do pass a syntax-only compile against the real 156 headers, using the `ardia/linux` target's flags from the existing out directory. Nothing was built or written there.

## What was done

### Native (`engine/ardiacore-linux/src/ardia/linux`)

- **`ac_linux_import_storage.{h,cc}`.** The scratch-copy, SQLite and file helpers moved out of the readers unchanged, so the new readers share them.
- **`ac_linux_import_crypto.{h,cc}`** (BoringSSL only):
  - **Values.**
    - `SecretBytes`, a move-only string that is wiped when destroyed or moved from.
    - The Chromium Safe Storage key: PBKDF2-SHA1(secret, `saltysalt`, 1 iteration, 16 bytes), as `freedesktop_secret_key_provider.cc` derives it.
    - `DecryptChromiumValue`/`Bytes`: `v10` uses the basic key and the empty-key fallback; `v11` uses the keyring key; `v12` is reported as the desktop portal. Cookie host hashes (DB version 24 and later) are verified and stripped.
  - **Firefox.**
    - A DER reader capped at depth 16.
    - NSS PBE: PBES2 with PBKDF2-HMAC-SHA256 and AES-256-CBC, including NSS's 14-byte IV, and the legacy SHA1-and-3DES scheme.
    - The password hash is SHA-1(salt + password), or **SHA-384 when the global salt is 48 bytes**. NSS 3.13x writes a 48-byte salt for primary-password databases; this came from `sftkpwd.c`.
    - The `password-check` test, choosing the key row (CKA_ID, skipping `as-logins-key`), and SDR field decryption (3DES or AES-256).
  - **mozLz4.** The LZ4 block decoder rejects bad offsets, overruns and size mismatches.
- **`ac_linux_import_keyring.{h,cc}`.** Secret Service and KWallet over GDBus, loaded with `dlopen("libgio-2.0.so.0")`, so there are no new link deps.
  - **Item names.** `SafeStorageFor` maps each browser id to its libsecret `application` attribute and its KWallet folder and key: "chrome" with "Chrome Keys"/"Chrome Safe Storage", and likewise chromium, brave, microsoft-edge, vivaldi, opera and helium.
  - **`Exists`, without consent.** It runs `SearchItems` (schema v2, then v1, then `application` alone) and KWallet `keyDoesNotExist`. It never unlocks, never opens a wallet and never reads a secret. Every call is `NO_AUTO_START` with a 3 s timeout.
  - **`Read`, after consent.**
    - The Secret Service path is `OpenSession("plain")`, then `Unlock` with the prompt's `Completed` signal awaited on a private main context (120 s deadline; past it, the prompt is dismissed and the result is `denied`), then `GetSecrets` and `Close`. The GVariant secret bytes are zeroed after copying.
    - The KWallet path is `open`, `readPassword` and `close`; a negative handle means `denied`.
  - **Outcomes:** `ok`, `missing` (`notFound`), `locked`, `denied`, `unavailable`.
- **`ac_linux_import_secrets.{h,cc}`.**
  - **Readers.**
    - Chromium `Cookies` (`Network/Cookies` first). It counts expired cookies and keyring, portal and undecryptable values, and keeps partition keys.
    - Firefox `cookies.sqlite`. Container and private cookies are skipped and counted; `partitionKey` is parsed, as on macOS.
    - Chromium `Web Data` addresses: the `addresses`, `local_addresses` and `contact_info` token tables and the legacy `autofill_profiles`, de-duplicated.
    - Chromium `Web Data` cards: `v10`/`v11`; locked cards are counted.
    - Firefox `logins.json` with `key4.db`, which reports `locked` without the right primary password.
  - **Payloads.** `CookiesPayload`/`AutofillPayload` build the JSON for the shared `ac_cookies_import`/`ac_autofill_import` engine services, the same wire format as macOS `CookieWire`/`AutofillWire`. They are `SecretBytes` and wiped after the call.
  - **Stores.**
    - `ImportedVault` is owner-bound, single use, lasts 10 minutes and holds 8 entries per window.
    - `UnlockedKeys` holds per-window, per-browser keys or primary passwords for 10 minutes.
  - **Leak gate.** `ReplyLeaksImported` rejects any reply string that contains a cookie value or a card's digits.
- **`ac_linux_import_sessions.{h,cc}`.**
  - Chromium SNSS, ported from macOS `ChromiumSessions`: versions 1 and 3, and version 5 (encrypted, which needs the key). It handles navigation pruning, pinned tabs, groups, last-active times and closed tabs and windows, and keeps normal windows only.
  - Firefox `recovery.jsonlz4`/`sessionstore.jsonlz4`/`previous.jsonlz4`, whichever is newest.
- **`ac_linux_import_readers.{h,cc}`.**
  - **Discovery.** It now advertises `tabs`, `cookies` and `autofill` (Chromium), and `tabs`, `cookies` and `passwords` (Firefox). It also sets `tabsNeedKey`, and sets `needsKeychain`/`keychainService` when the keyring probe finds a Safe Storage item.
  - **Kinds.** `ReadBrowserProfile` dispatches each new kind.
  - **Warnings.**
    - `locked`: a keyring value, card or session without the unlocked key, or a Firefox primary password.
    - `undecryptable`: the desktop portal, or a bad value.
    - `skipped`: container cookies.
    - `empty`: no store in the profile, which used to be reported as `unsupported`.
- **`ac_linux_import_service.cc`.**
  - **`unlockBrowser`.**
    - Chromium: it replies `true`, then reads the keyring on a `CONTINUE_ON_SHUTDOWN` pool task (so a waiting prompt never blocks quitting) and derives the key. The key is stored per window and the result is announced by event.
    - Firefox: the primary password is stored as given.
    - A per-window generation, bumped by `forgetUnlockedKeys` and by closing the window, drops results that arrive late.
  - **`startImport`.** It copies the window's key into the job. A result with cookies, addresses or cards goes into the imported vault and returns `vaultToken`.
  - **`writeImported`.** It takes the vault entry (private and invalid targets lose it), resolves the profile through #62's registry, and writes 500-item batches through `ac_engine_call("ac_cookies_import" | "ac_autofill_import")`. It finishes with an `importedWritten` event.
  - **`discardImported`.** Discards a vault entry.
  - **Cleanup.** Window close, `forgetUnlockedKeys` and termination wipe keys and vault entries.
- **`BUILD.gn`.** Lists the new sources. No new deps: `//third_party/boringssl` and `//third_party/sqlite` were already there, and GIO is loaded with `dlopen`.

### Linux UI (`apps/linux/src/hostImport.ts`)

- **`unlockBrowser`.**
  - It resolves at once for Chromium sources without `needsKeychain`.
  - Otherwise it invokes the native unlock and waits for `unlocked`/`failed`, up to 150 s.
  - Firefox passes `primaryPassword`.
- **`isBrowserUnlocked`.** True after a successful unlock, or always for a Chromium source with no keyring item. `forgetUnlockedKeys` clears it.
- **`writeImported`/`discardImported`.** `writeImported` is event-completed and returns the shared `WrittenCounts`. The shared `importNative` already calls it with the result's `vaultToken`.
- **Results.** They keep a well-formed `vaultToken` and the cookie, address and card counts.

## Contract

| Method | Args | Reply, then events |
| --- | --- | --- |
| `ArdiaImport.unlockBrowser` | `requestId, browserId, primaryPassword \| null` | `true`, then `{type:"unlocked"}` or `{type:"failed", code: locked\|denied\|notFound\|unavailable\|cancelled}`; immediate `busy`/`unsupported`/`invalid` |
| `ArdiaImport.startImport` | unchanged | `done` results may carry `tabs`, `tabGroups`, `cookieCount`, `addressCount`, `cardCount` and `vaultToken` |
| `ArdiaImport.writeImported` | `requestId, token, engineProfile, kinds ⊆ [cookies, autofill]` | `true`, then progress and `{type:"importedWritten", cookies, cookiesRejected, cookiesExisting, addresses, cards, cookiesFailed?, autofillFailed?}`; immediate `expired`, `ERR_PROFILE_PRIVATE`, `ERR_PROFILE_INVALID`; registry `ERR_PROFILE_*` as events |
| `ArdiaImport.discardImported` | `token` | boolean |
| `listBrowsers` | unchanged | Chromium sources set `needsKeychain`/`keychainService` when a Safe Storage item exists; profiles may set `tabsNeedKey` |

## Where secrets go

- **Safe Storage secret.** It is read on a pool thread, derived into the key at once, and wiped. Only the key is kept, per window, for 10 minutes or until `forgetUnlockedKeys` or window close. The renderer never sees it.
- **Firefox primary password.** It arrives through the typed invoke, is held like a key, and is never echoed.
- **Values.** Cookie values and card numbers stay in `SecretBytes` inside the vault. They leave the vault only as the engine payload, which is wiped after `ac_engine_call` copies it. Imported replies go through the leak gate, which rejects cookie values and card digits as well as the password checks.
- **Logging.** Nothing is logged; a source check enforces it.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_import_followups test_import` | 33 pass, under ASan/UBSan with the system SQLite, OpenSSL and GIO. The changed `test_import` expectations: `empty` instead of `unsupported`, `locked` for keyring passwords, and the portal note. |
| What the 13 new tests cover | **Keyring.** A real `dbus-run-session` with a fake Secret Service/KWallet in Python (`fixtures/fake_keyring.py`) covers unlocked, prompt accepted, prompt dismissed, a prompt that never answers (dismissed after the deadline), probes that never unlock, missing items, a different browser, KWallet reading and a refused open, and no service. **Firefox.** Genuine NSS profiles (`certutil` plus `PK11SDR_Encrypt` through ctypes, `fixtures/nss_logins.py`) with and without a primary password, a wrong password, and a hand-built legacy 3DES `key4.db`. **Sessions.** Chromium SNSS v3 and encrypted v5. **Firefox session.** mozLz4 made by the real liblz4, plus corrupt blocks. **Cookies and autofill.** Every field, partitions, containers, the portal, host-hash mismatches and expiry. **The rest.** Payload JSON, the leak gate, the vault, unlocked keys, the source rules and the service wiring. |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | See the PR description for the final count. |
| `pnpm --filter @ardia/linux test:host` | 368 passed. The new `hostImportFollowups.test.mjs` adds 5. |
| `pnpm --filter @ardia/linux typecheck`, `build`, `test:graph` | Pass (graph: 3). |
| `pnpm --filter @ardia/linux acceptance:self-test` | 68 passed. The 5 new tests cover the fixture generator, NSS generation and the fake keyring on a private bus. |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 8 passed |
| Syntax check against 156 | `-fsyntax-only` with the real 156 headers passes for crypto, storage, secrets, sessions, keyring, readers and service. |
| Cross-check | The native reader over the acceptance generator's scratch profiles returns exactly the counts I109/I110/I111 expect. |

## Needs native runtime acceptance on a Chromium 156 build

- Compile and link, including `dlopen` of GIO from inside the browser process.
- `acceptance --only I109,I110,I111`. Also F12: its expectations changed to `locked` and `empty`.
- The manual steps in `acceptance-suite.md`:
  - GNOME Keyring and KWallet with real prompts;
  - real Chrome and Firefox profiles;
  - the import window click-through.

## Follow-ups opened

- #178: the import window still says "macOS … keychain" on Linux, and has no Firefox primary password prompt (the native path supports one).
- #179: `v12` (desktop portal) values can't be read across applications; they're counted and reported.
- #180: favicons, Firefox addresses and cards, and Firefox `logins.db`.

## Risks

- **Item names.** The `application` attribute for Edge, Vivaldi, Opera and Helium follows Chromium's product-name pattern. Only Chrome, Chromium and Brave are well documented.
- **GDBus from a pool thread.** The keyring code subscribes on a private `GMainContext`. It uses the process's shared session-bus connection, which GTK may also use.
