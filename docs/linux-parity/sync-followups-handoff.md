# Sync follow-ups handoff: passwords, large state, Linux wording (#145, #146, #147)

These are follow-ups to the Linux sync port (`p-sync-handoff.md`, #138 for #23). All three were built and tested from source. The native changes pass `scripts/linux/syntax-check-156.sh`, but none has run in a Chromium 156 build yet. Runtime acceptance stays with #23; the manual rows are in `acceptance-suite.md` under #23 F13.

## #145 Saved passwords sync through a sealed native path

Before, `ArdiaSync.readPasswords` returned `null` and `savePassword` returned `false`, so the shared passwords adapter skipped passwords. Both are now typed routes (`Route::kPasswords`) that only the sync lease holder may call. Anyone else gets `ERR_SYNC_NOT_AUTHORITY`, and private windows get `ERR_PRIVATE_SERVICE` as before.

### Reading: `readPasswords(profile)`

1. `ac_linux_sync_service.cc` validates the profile name (`ReadPasswordsProfile`, at most 256 bytes) and calls `LinuxCredentialsReadLogins` in `ac_linux_credential_service.cc`.
2. That resolves the profile with the credential resolver (`ResolveCredentialProfile`), so private and off-the-record profiles are refused. It then runs the same store probe as the other password calls (`WithProbes` and `Assess(kPasswords)`). A store that is unavailable, still loading after 10 s, or without a working key gives an incomplete read.
3. `SyncLoginReader` calls `PasswordStoreInterface::GetAutofillableLogins`. This never asks for authentication, unlike `ac_passwords_reveal`. Blocklisted and federated rows are skipped. Each password goes from `PasswordString::secure_value()` (a wiping `SecureU16String`) through `SecretUTF8` into a `linux_sync::StoredLogin`. A `StoredLogin` wipes its password when destroyed, and wipes the source when moved or copied in, so no stray copy is left in a moved-from short string. The reader gives up after 30 s.
4. The rows move to the sync sequence, where `SyncCore::SealLogins` seals each password with `SyncKeys::SealPassword` and wipes the plaintext. JS receives only `[{origin, username, sealed, created}]`. `created` is milliseconds since the Unix epoch, as on macOS.

**When a read is refused.** `null` makes the shared adapter skip passwords this cycle, so nothing is read, deleted or overwritten. A read is refused when:

- the sync key is locked;
- the store errors or is unavailable;
- the reader times out;
- `kSkipUndecryptablePasswords` is enabled.

The last case matters because of how Chromium 156's `LoginDatabase` handles a row that fails to decrypt. By default on Linux it fails the whole read. Only with `kSkipUndecryptablePasswords` does it return the other rows as if nothing happened, and the consumer can't tell. Refusing whenever that feature is on is the Linux equivalent of macOS's `undecryptable > 0` check.

**Which rows are kept.** These follow macOS's `loginOrigin`:

- the trailing slash is dropped, and then the realm must be `http(s)://host[:port]` with no path, whitespace or control character (so HTTP-auth realms and Android rows are skipped);
- empty passwords are skipped;
- the first row wins for each origin and username;
- passwords over 4 KiB and usernames over 1 KiB are skipped.

### Saving: `savePassword(profile, origin, username, sealed, replacing)`

1. `ParsePasswordSave` checks the arguments. The origin must already be a login origin, the username is at most 1 KiB, `sealed` is at most 16 KiB, and `replacing` is a boolean. Anything else gets `ERR_SYNC_ARGUMENT`.
2. On the sync sequence, `SyncCore::OpenLogin` opens the sealed value with the origin and username as AAD. A locked key, tampering, a different origin or username, or an empty password returns `false`, and nothing is saved.
3. On the UI thread, `LinuxCredentialsSaveLogin` builds the existing typed `ArdiaPasswords.save` plan, which reaches `ac_passwords_add` with `passwordHex` in a wiping buffer (F07). When `replacing` is true it first runs the `passwords.remove` plan (`ac_passwords_remove`). This is macOS's delete-then-save, and it is needed because `ac_passwords_add` refuses a login that already exists. Neither call prompts.

Deletions still go through the shared adapter's `deletePassword`, which uses the generic `passwords.remove` route and never carries a secret.

`status().passwords` is now `true`.

### What isn't wiped

- Chromium's own copies, as in `f07-security-notes.md`: the `StoredCredential` vector the store returns (its `PasswordString` is cleared, not cleansed); the UTF-16 inside `ac_passwords_add`; and the IPC buffers of the sealed values, which aren't secret.
- The base64 sealed strings, which are ciphertext.

### Tests

- `tests/fixtures/sync_scenarios.cc` (`SealedLogins`):
  - origin filtering and argument parsing;
  - locked and partial reads give `null`;
  - three of seven rows survive, and sealing is deterministic;
  - non-ASCII passwords and empty usernames round-trip;
  - wrong origins and usernames, tampered and non-base64 values, and opening after `forget` all fail;
  - a moved `StoredLogin` leaves an empty source;
  - the output never contains a password.
- `tests/sync_devices.test.mjs` adds a second scenario. Two device processes on the real `SyncCore` sync passwords through the real `syncScope` merge. The device tool keeps a fake login store natively, so JS only handles sealed values. The scenario covers:
  - setup and join;
  - an HTTP-auth realm that is never synced;
  - a password changed on the other device replaces the old one;
  - no password, origin or username anywhere in the folder.
- `test_sync.py` checks the source:
  - the lease gate comes before the password routes;
  - the reader uses `GetAutofillableLogins`, `secure_value()` and `SecretUTF8`, never `password_value.value()`;
  - the read refuses under `kSkipUndecryptablePasswords` and checks the store with `Assess`;
  - the save uses the typed plans;
  - the sync service never touches a `.password`;
  - nothing logs.
- `src/hostSync.test.mjs`: both methods are typed invokes that carry only sealed values.

## #146 The sync state may grow past 8 MiB

- **The limit.** It is now per name: `linux_policy::DocumentLimit(name)` in `ac_linux_policy.h`. `sync-state.nns` may be up to 48 MiB, and everything else keeps 8 MiB. 48 MiB covers the base64 of the largest state `sealLocal` accepts: a 32 MiB payload framed and padded to 1 KiB, plus the NNS1 magic, nonce and tag. `test_host_policy.py` checks this at compile time against the sync constants.
- **Where it applies.** `ReadDocument`, `WriteDocument` and the `ArdiaShell.writeDocument`/`saveDocument` branch all use the per-name limit.
- **The window storage ledger.** The ledger keeps the operation of every pending write to match retries and recover failures. It caps one intent at 16 MiB and the whole ledger at 32 MiB, so it would still have refused a large state. A write above 8 MiB, which only the sync state can be, now records `{"kind":"write","sha256":…}` instead of its contents. Matching and recovery are unchanged: only a later successful write of the same bytes clears a failure. While one such write is pending in a window, another gets `ERR_DOCUMENT_BUSY`, which bounds the memory held. The shared engine already serializes its state saves, and `hostDocuments.ts` serializes every write per renderer. Writes up to 8 MiB are tracked exactly as before.
- **The bootstrap snapshot.** It skips `sync-state.nns`, so no window reads up to 48 MiB at startup. The sync authority re-reads it through `refreshDocument` before the engine starts, and no other renderer reads it.
- **Tests.**
  - `test_window_storage.py` (`large-sync-state`): 8 MiB + 1 is refused for other names; 9 MiB and exactly 48 MiB states are written, while 48 MiB + 1 is refused; the ledger holds a digest; a second large write in flight is refused; a failed large write is recovered only by the same bytes.
  - `sync_scenarios.cc`: a 12 MiB state seals to more than 8 MiB and reopens.
  - `test_document_authority.py`: the bootstrap never reads the state.

## #147 Linux wording and kit actions in the Sync pane

`apps/linux/syncWording.ts` rewords shared sources at build time, the way `credentialWording.ts` (#160) rewords the credential panes. The shared panes aren't forked. `credentialWording.ts` now exports the small factory both use (`rewordWith`, `wordingPlugin`). A replacement that no longer matches fails the Linux build and names the file to update. So does a listed file that was never transformed.

| Source | Change on Linux |
| --- | --- |
| `panes/Sync.tsx` | Computers and devices instead of Macs, and Syncthing, Nextcloud or Dropbox instead of iCloud Drive. The iCloud-off warning means the default folder's parent is missing on Linux, so it now says that and offers Choose… instead of opening macOS System Settings. Open Sync Folder replaces Show Sync Folder in Finder. "Add to This Computer", "(This Computer)" and "Just this computer so far" replace the Mac labels. |
| `panes/SyncSheets.tsx` | The Recovery Kit sheet calls itself a text file and offers Close and Save as Text… only. The PDF preview and the Copy and Share kit actions are gone, because they return `null` on Linux. Show Words, Show Code and Copy Words stay. Elsewhere: the system keyring instead of the Keychain, your phone's camera instead of the iPhone and Universal Clipboard, your other device instead of your other Mac, `~` for `/home/<user>`, and a monospace font instead of Menlo. |
| `rows/generalRows.ts` | The Sync search row says computers, with Syncthing and Nextcloud keywords instead of iCloud. |
| `state/sync/syncDoc.ts`, `state/sync/engine.ts` | The Passwords type says "this computer", and the locked message says "this device". The engine's Keychain message for a failed `createPhrase` stays, because `syncEngine.ts` matches it and swaps in the keyring message. |

A PDF kit isn't built; the text kit is the only format.

**Tests.** `src/syncWording.test.mjs` (5 tests, now in `test:host`) checks:

- the reworded pane and sheets match no Mac, iCloud, Finder, iPhone, Keychain or `/Users` wording;
- the kit sheet has no PDF, preview, copy or share call;
- every replacement still matches;
- the Vite config runs the transform;
- a build fails on a file that was never reworded.

The production bundle was checked for the new strings and for the absence of the old ones.

**Visual check still needed.** I couldn't run a Chromium build here, so nobody has looked at the reworded pane in a window: the kit sheet without its preview column, and the folder warning row.

## Not done

- **#174.** A sealed-password vector produced on a Mac can't be made on Linux; the issue stays open. The Linux and macOS formats are still checked against the independent `node:crypto` reference, and the new two-device scenario uses the same `SealPassword` code.

## Test runs

| Command | Result |
| --- | --- |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | 469 of 475 pass. The 6 failures also fail on the base: five patch dry-runs fail because the reference 156 tree already has those patches applied, and `test_downloads` expects a `//components/download/public/common` dependency the base BUILD.gn doesn't list. After the rebase, `test_sync`, `test_window_storage`, `test_document_host`, `test_document_authority`, `test_host_policy`, `test_quit` and `test_chromium_close` pass. |
| `pnpm --filter @ardia/linux test:host` | 459 of 459 pass after the rebase. |
| `pnpm --filter @ardia/linux acceptance:self-test`, `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 79 of 79 and 8 of 8 pass. |
| `pnpm --filter @ardia/linux typecheck`, `build`, `test:graph` | Typecheck and build pass; `test:graph` passes 3 of 3. |
| `scripts/linux/syntax-check-156.sh ac_linux_sync ac_linux_credential_service ac_linux_host.cc` | All 9 files pass. |
