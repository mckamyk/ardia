# Import follow-ups and Linux wording handoff (#178, #179, #180, #151)

Branch `t3/linux-import-wording`. Follows [U-import-handoff.md](U-import-handoff.md) (#158) and [f12-import-handoff.md](f12-import-handoff.md). Nothing here ran inside a Chromium 156 build: the native sources pass `scripts/linux/syntax-check-156.sh` and run under ASan/UBSan against system SQLite, OpenSSL and NSS-generated profiles.

## #178: unlock step on Linux, and a Firefox primary password prompt

- **Linux-owned unlock step.** `apps/linux/src/LinuxUnlockStep.tsx` replaces `components/import/steps/UnlockStep.tsx` through `vite.config.ts`, so the shared step is never in the Linux graph (`scripts/module-graph.test.mjs` checks it). The wording and the logic live in `apps/linux/src/importUnlock.ts`:
  - Chromium sources name the system keyring (Secret Service or KWallet) and the Safe Storage item.
  - Firefox sources ask for the primary password only when a selected profile has one (`needsPrimaryPassword`), and mention the `Firefox Encrypted Storage` keyring item when a profile keeps card numbers there (`cardsNeedKeyring`).
  - Failures are worded per native code: `locked`, `denied` (wrong primary password, or the keyring refused), `notFound`, `unavailable`, `timeout`, `cancelled`.
  - "Skip Encrypted Data" drops passwords (and, for Chromium, cookies, as the shared step does). Firefox keeps cookies, which it doesn't encrypt.
- **The typed password.** It sits in a module-level slot (not React state, never persisted), is taken once per attempt and cleared after every attempt, on Skip and when the step unmounts. It reaches the host only as the third argument of `ArdiaImport.unlockBrowser`, the existing typed invoke. Nothing logs it; a test checks the sources for logging, telemetry and storage calls.
- **Native.**
  - Discovery marks a Firefox profile `needsPrimaryPassword` when its `key4.db` password check doesn't open with the empty password, and sets the source's `needsKeychain`, so the shared flow stops at the unlock step.
  - `unlockBrowser` for Firefox now runs on the keyring pool (`ReadFirefoxSecrets` → `UnlockFirefox`). A given password is checked against every profile that needs one; if it opens none, the unlock fails `denied` ("That isn't Firefox's primary password") and nothing is stored. Before, any string was stored and the import later reported `locked`.
  - The password is kept per window and per browser, as before. One password per browser: profiles with a different primary password still report `locked`.
  - Results that arrive after `forgetUnlockedKeys` or window close are dropped by the existing per-window generation.

## #179: Chromium data protected by the desktop portal (`v12`)

There is no supported path, so the importer keeps counting and reporting it:

- The `org.freedesktop.portal.Secret` interface (`RetrieveSecret`) returns a secret for the calling application only; the portal frontend derives the app ID from the caller (Flatpak ID or systemd scope). Chromium derives its `v12` key with HKDF-SHA256 from that secret (`secret_portal_key_provider.cc`), so Ardia calling the portal gets Ardia's secret, never Chrome's.
- Reading the portal backend's own store (for example gnome-keyring's `org.freedesktop.impl.portal.Secret` items) would bypass the per-app isolation the portal promises, is backend-specific, and is out of reach for sandboxed sources anyway. We don't do it.
- The warnings now say why and what works instead:
  - passwords: "... The desktop portal gives each app its own key, so Ardia can't read Google Chrome's. Export them from Google Chrome's password manager as a CSV file and import that file." (the CSV import path is supported, F12);
  - cookies: "... Sign in to those sites again in Ardia.";
  - cards: "... Add those cards again in Ardia."

## #180: favicons, Firefox addresses and cards, Firefox `logins.db`

- **Favicons.** When bookmarks or tabs are imported, `ReadFavicons` reads Chromium's `Favicons` or Firefox's `favicons.sqlite` (snapshot copy, as every other store) for the open tabs and the toolbar bookmarks, the same pages macOS `ChromiumFavicons.pages` picks. It ports macOS's rules: exact page, then the page without its fragment, then the same origin (its root page first); favicon icons only on Chromium (`icon_type = 1`); prefers the smallest bitmap at least 32 px wide, else the largest; PNG only, 16 KB per icon, 6 MB and 1000 pages in total. The result's `favicons` feed the shared `applyResult` (`adoptFavicons`). `hostImport.ts` keeps only entries with an http(s) page, a string icon URL and base64 PNG data.
- **Firefox `logins.db`** (the application-services Rust store desktop Firefox is moving to). Its key is the AES-256 secret key NSS keeps in `key4.db` under the label `as-logins-key`, wrapped like the other key rows by the primary password. Each row's `secFields` is a compact JWE (`dir`, `A256GCM`, the protected header as AAD) of `{"u": username, "p": password}`. The reader takes live `loginsL` rows and `loginsM` rows that aren't overridden or shadowed by a local row. It merges with `logins.json` when both exist and drops duplicates. A store that can't be read while the other one could is reported as an `unreadable` warning.
- **Firefox addresses and cards.**
  - `autofill.db` (Rust store): `addresses_data`, and `credit_cards_data` whose `cc_number_enc` is a JWE under the NSS key `as-autofill-key`. A primary password locks the cards (counted, "Enter Firefox's primary password to import N saved cards"); addresses import regardless.
  - `autofill-profiles.json` (older JSON store): live addresses (tombstones skipped, name parts joined) and cards whose `cc-number-encrypted` is Firefox OSKeyStore output: base64 of a 12-byte IV, AES-128-GCM ciphertext and a 16-byte tag. Its key is the base64 secret of the libsecret item `{xdg:schema: mozilla.firefox, string: "Firefox Encrypted Storage"}` (`LibSecret.cpp`). JSON cards are used only when `autofill.db` has none, so a migrated profile never asks for the keyring.
  - Discovery adds `autofill` for Firefox profiles with either store, marks `cardsNeedKeyring` when only the JSON store holds encrypted cards, and sets `needsKeychain`/`keychainService` only when the keyring probe (no unlock, no secret read) finds that item. The consented unlock reads it with the existing Secret Service prompt path. If the keyring refuses but the primary password worked, the unlock still succeeds and the cards are reported `locked`; if the keyring was the only reason to unlock, its code is the failure.
  - The card key is stored per window as `<browser>#cards` beside the primary password, wiped with it, and copied into the job as `firefox_card_key`. Cards go into the existing imported vault and through the leak gate like Chromium's.
  - Item labels follow `MOZ_APP_BASENAME`: Firefox and Firefox Snap use `Firefox Encrypted Storage`, LibreWolf `LibreWolf Encrypted Storage`, Zen `Zen Encrypted Storage`. Flatpak Firefox stores it through the Secret portal in its own sandbox keyring, which we can't read, so its JSON cards stay `locked`.

## #151: shared copy that described macOS

`apps/linux/linuxWording.ts` is a second build-time transform beside `credentialWording.ts` (`linuxWording()` in `vite.config.ts`). It rewords:

| Shared file | Copy |
| --- | --- |
| `components/onboarding/SourceStep.tsx` | "Found on this Mac" → "Found on this computer" |
| `components/onboarding/LayoutSteps.tsx` | "like your Mac" → "following your desktop" |
| `components/settings/panes/Appearance.tsx` | "Follow macOS" → "Follow system" |
| `components/settings/rows/generalRows.ts` | the scroll bar description |
| `components/settings/panes/Privacy.tsx` | "Blocking happens on this Mac" |
| `components/settings/rows/privacyRows.ts` | the What's Sent row |
| `state/telemetry/copy.ts` | What's Sent and the onboarding sharing copy ("this Mac", "macOS version") |
| `components/import/steps/ChooseStep.tsx`, `SafariStep.tsx`, `importActions.ts`, `diaActions.ts` | the import flow's Mac and keychain messages |

- The unlock step's copy is in the Linux-owned step above.
- Settings › Engines (its passkey messages name the Mac and iCloud Keychain) isn't offered on Linux: `unavailablePanes` hides it without WebKit, which a test asserts.
- Sync copy is #147.
- Like `credentialWording.ts`, a replacement whose source string disappears throws during the transform. A production build that loads any settings, import or onboarding module but never reworded a listed file fails in `buildEnd`. Every replacement must match exactly once in its shared file (`linuxWording.test.mjs`).

## Review round 1

- **The Firefox unlock is scoped to the selection.** `unlockBrowser` now carries the selected profile ids and kinds (`[requestId, browserId, primaryPassword, profileIds, kinds]` for Firefox; the native side keeps valid ids and `passwords`/`cookies`/`autofill` only).
  - The typed password is checked only against selected profiles that need one, so a password that opens only an unselected profile is `denied`.
  - The `Firefox Encrypted Storage` key is read only when `autofill` is chosen and a selected profile has `cardsNeedKeyring`; otherwise the keyring isn't touched at all.
  - The unlock copy mentions the keyring item under the same rule. An empty profile list means all profiles, as in the copy.
- **A refused card key doesn't count as unlocked.** If the primary password opened but the keyring refused the card key, the `unlocked` event carries `cardsLocked: true` and `hostImport.ts` leaves the browser locked, so the next import goes back to the unlock step.
- **`logins.json` with no entries** beside `logins.db` no longer derives the SDR key, so it no longer warns "key4.db holds no key".
- **Duplicates.** `logins.json`/`logins.db` duplicates (realm + username) keep the most recent one (`max(timeLastUsed, timeCreated)`), and `kMaxCredentials` applies after de-duplication; each reader caps only its own rows.
- **Wiping.** The decrypted `p` field of each `logins.db` row is wiped in its parsed `DictValue` after the copy.
- **Bounds and fallbacks.** JSON expiry numbers outside 0–9999 (and non-finite ones) read as 0 instead of an out-of-range cast. An unreadable `autofill.db` is an `unreadable` warning and `autofill-profiles.json` still imports.
- **Keyring search scope.** The card-key search always names `xdg:schema: mozilla.firefox`. Deb and Snap Firefox share that label and schema, so on a system with both the first match wins, as it does for Firefox itself.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_import_firefox_stores` | 20 pass (8 added in review round 1); covers `logins.db` (plain, primary password, merged with `logins.json`, missing key), `autofill.db` with and without the primary password, `autofill-profiles.json` with and without the card key, the Firefox unlock (wrong password, right password, keyring accept, dismiss, missing item, partial success) over a private D-Bus with the fake keyring, discovery flags, Chromium and Firefox favicons, portal guidance and source rules. Keys are real NSS token keys (`fixtures/nss_app_keys.py` uses `PK11_ImportSymKeyWithFlags` and `PK11_SetSymKeyNickname`, as application-services does); JWE and OSKeyStore ciphertexts come from libcrypto through ctypes. |
| `python3 -m unittest test_import test_import_followups test_import_firefox_stores` | 59 pass under ASan/UBSan, with the two portal message expectations updated |
| `pnpm --filter @ardia/linux test:host` | 481 pass after rebasing on `ea8ce3c6`; includes `importUnlock.test.mjs`, `linuxWording.test.mjs` and the favicon filter test in `hostImportFollowups.test.mjs` |
| `pnpm --filter @ardia/linux typecheck`, `build`, `test:graph` | pass (graph: 3) |
| `pnpm --filter @ardia/linux acceptance:self-test` | 79 pass |
| `scripts/linux/syntax-check-156.sh ac_linux_import_` | 8 of 8 files pass |

## Needs runtime acceptance on a Chromium 156 build

- The import window click-through on Linux: keyring prompt wording, the Firefox primary password field, a wrong password, Skip.
- A real Firefox profile with `logins.db`/`autofill.db` (current Firefox release with the Rust stores on) and an older one with `autofill-profiles.json` cards under GNOME Keyring and KWallet.
- Favicons showing on imported toolbar bookmarks and tabs.

## Risks

- The application-services stores are new on desktop. Their table names, the JWE header and the `as-*-key` labels come from application-services `main`; a later schema change would show up as `unsupported`/`unreadable` warnings, not silent loss.
- The LibreWolf and Zen keyring labels assume their `MOZ_APP_BASENAME`.
- Discovery now copies each Firefox profile's `key4.db` (and `autofill.db` when the JSON store has encrypted cards) to check for a primary password: a few hundred KB per profile, removed at once.
- The renderer's copy of the typed primary password and the `base::Value` holding it in the host message aren't wiped (the same gap f12 lists for reply values).
