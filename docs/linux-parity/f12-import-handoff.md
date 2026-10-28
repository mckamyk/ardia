# F12 handoff: Linux data import backend

Issues: [#22 F12](https://github.com/mckamyk/ardia/issues/22) (stays open until runtime acceptance), [#95](https://github.com/mckamyk/ardia/issues/95) (source implementation). Branch `t3/linux-import`.

## What changed

Before this branch, every `ArdiaImport.*` call fell through `ShellHostView::Invoke` to "Unsupported Linux host method". The import window's browser list was therefore empty, and bookmark HTML import failed.

### Native (`engine/ardiacore-linux/src/ardia/linux`)

- **`ac_linux_import_formats.{h,cc}`.** These pure helpers have no Chromium dependencies beyond `base::Value`:
  - Netscape bookmark HTML parser: nested folders, toolbar and other roles, entities, comments, and Latin-1 input. Depth is capped at 64 and the node count at 250,000.
  - Password CSV parser: Chrome, Firefox and Bitwarden headers, quoted multiline fields and a BOM. It counts rows, invalid rows and in-file duplicates, and skips Bitwarden non-login rows.
  - `profiles.ini` reader.
  - Profile classifier: `""`/`default` is the default profile, `incognito:` is private, an id is isolated, anything else is invalid.
  - Request validation.
  - The single-use chosen-file ledger.
  - The credential vault: owner-bound, 10-minute lifetime, 8 tokens per window. Its secrets are wiped on destruction.
- **`ac_linux_import_readers.{h,cc}`.** Discovery and readers, using SQLite (`//third_party/sqlite`) and BoringSSL:
  - Chromium family under `$XDG_CONFIG_HOME`: Chrome (stable, beta, dev), Chromium, Brave (three channels), Edge (three), Vivaldi (two), Opera and Helium. Also Chrome, Chromium and Brave as Flatpak, and Chromium as a Snap.
  - Firefox family: Firefox (native, Flatpak, Snap), LibreWolf and Zen.
  - Ardia's own user-data directory is never listed.
  - Readers:
    - Chromium `Bookmarks` plus `AccountBookmarks`.
    - History (`urls`) with limit and since.
    - `Login Data` plus `Login Data For Account`. `v10` (basic) values are decrypted with the "peanuts" key and, like Chromium's `Encryptor`, the empty-password fallback key. `v11` and `v12` (keyring or portal) values and failures are counted and reported. Duplicates are removed.
    - Firefox `places.sqlite` bookmarks and history.
    - Web Store extensions from `Preferences` and `Secure Preferences`.
  - Files are opened with `O_NONBLOCK`, and anything that isn't a regular file (a FIFO, for example) is refused instead of blocking. Ardia's own data directory is never listed or read.
  - Every database is copied (`O_NOFOLLOW`, read-only source) into a fresh `ardia-import-XXXXXX` directory under `TMPDIR`, together with its `-wal` and `-journal`, and opened there. The directory is removed when the read ends, including on cancellation and errors. Copies older than an hour that a crash left behind are swept at the next import.
- **`ac_linux_import_service.{h,cc}`.** The Chromium glue:
  - Jobs run on the thread pool and report progress events.
  - Cancellation uses an atomic flag checked every 128 rows and between kinds.
  - A `ui::SelectFileDialog` chooser uses Chrome's select-file policy, so it gets the portal or GTK dialog.
  - Passwords are written with Chromium's own `password_manager::PasswordImporter` into the profile store. Conflicts keep the stored password.
  - The writer observes profile destruction.
- **`ac_linux_host.cc`.** Four registration hunks: includes, bootstrap capabilities, the `Invoke` route and window-close cleanup (`LinuxImportWindowClosed`).
- **`BUILD.gn`.** New sources and deps: `//third_party/sqlite`, `//third_party/boringssl`, the password importer, `passwords_private`, `select_file_policy` and `//ui/shell_dialogs`.

### Linux UI (`apps/linux`)

- **`src/hostImport.ts`.** Implements the `@ardia/import` API on the host. Imports and the chooser reply at once and finish with `ArdiaImport/onImportEvent`, because host requests time out after 30 s. A stalled job is cancelled after 120 s without events. Safari, Dia, cookies and autofill reject with `unsupported`.
- **`src/shims/import.tsx`.** Uses that implementation when `window.__ardiaHost` exists and registers the password sink.
- **`src/importApply.ts`.** Replaces `state/import/apply.ts` through `vite.config.ts`. Its `applyResult` calls the shared one, then writes the pending password token into the destination profile and adds Chromium's imported count to `counts.passwords`. Unrequested tokens are discarded. A private destination or a failed write marks passwords failed.

## Contract

All methods are `__ardiaHost.invoke("ArdiaImport.<method>", { args })` from the privileged shell only. They are listed in the bootstrap `capabilities`. Errors reject with `{ code, message }`.

| Method | Args | Reply |
| --- | --- | --- |
| `listBrowsers` | — | `BrowserSource[]` (`family` `chromium`/`firefox`, `needsKeychain:false`, `available` ⊆ bookmarks, history, passwords) |
| `startImport` | `jobId, browserId, profileId, kinds[], {historyLimit?, historySince?}` | `true`, then events |
| `cancelImport` | `jobId` | `true` when a job was running |
| `writePasswords` | `requestId, token, engineProfile` | `true`, then progress events and `{type:"written", added, existing, conflicts, invalid}` or `{type:"failed", code}`. It writes in chunks of 500 through Chromium's `PasswordImporter`. `added` counts new rows; `existing` counts rows already saved with the same password, which Chromium reports as imported. Errors: `expired` and `ERR_PROFILE_PRIVATE`/`ERR_PROFILE_INVALID` (immediate); `ERR_PROFILE_*` from the registry, `ERR_PASSWORDS_UNAVAILABLE` and `ERR_PASSWORDS_IMPORT` (as events) |
| `discardPasswords` | `token` | boolean |
| `forgetUnlockedKeys` | — | discards the window's tokens |
| `chooseImportFile` | `requestId, "bookmarksHTML" \| "passwordsCSV"` | `true`, then `{type:"chosen", path \| null}`. A missing or refused chooser arrives as `path: null`. Errors: `ERR_FILE_CHOOSER_DISABLED` (policy), `busy`, `unsupported` |
| `cancelChooser` | `requestId` | `true` when that window's open chooser was dropped; the adapter calls it when it gives up waiting |
| `importBookmarksHTML` | `path` (chosen) | `{root, links}`; errors `ERR_FILE_NOT_CHOSEN`, `empty`, `tooLarge`, `unreadable` |
| `importPasswordsCSV` | `path` (chosen) | `{passwords: {token, count}, entries: [{url, username}], rows, duplicates, invalid}`. No password is ever in the reply. Same errors as `importBookmarksHTML` |
| `listExtensions` | `browserId, profileId` | `ImportedExtension[]` |

Events arrive on module `ArdiaImport`, event `onImportEvent`, with `jobId` set to the job or request id:
- `{type:"progress", kind, phase, processed, total?}`
- `{type:"done", result}`. `result` is the shared `ImportResult` with `credentials: []` and, when passwords were read, `passwords: {token, count}`.
- `{type:"failed", code, message}`. `code` is `cancelled`, `notFound` and so on.
- `{type:"chosen", path}`

**Profile resolution.** `writePasswords` resolves its target in `ac_linux_import_service.cc`, in this order:

1. A private (`incognito:`) id fails with `ERR_PROFILE_PRIVATE`, and an invalid id with `ERR_PROFILE_INVALID`. Either way the token is discarded.
2. A token the window doesn't hold fails with `expired` before any profile is resolved, so a bogus request never creates an isolated profile.
3. `ResolveImportProfile` hands `""`/`default` and isolated ids to #62's registry (`ardiacore::ResolveLinuxProfile`). That loads or creates `<user data>/Profile <id>`.
4. A registry error keeps its `ERR_PROFILE_*` code. An off-the-record result fails closed with `ERR_PROFILE_PRIVATE`.

So "Import as new profile" in the shared flow stores the passwords in that profile ([#112](https://github.com/mckamyk/ardia/issues/112)).

## Where a secret goes

1. A browser profile's `Login Data` is copied, encrypted, into the private scratch directory.
2. On a pool thread, values are decrypted into `Credential` objects. These are move-only and wipe the password and note on destruction and after moves.
3. The decrypted credentials move to the UI thread and into the vault under an unguessable 128-bit token bound to the requesting window.
4. The renderer only ever sees `{token, count}`. Tests assert that the serialized result contains no fixture password.
5. `writePasswords` takes the credentials out of the vault (single use) and hands them to `PasswordImporter`, which stores them through `SavedPasswordsPresenter` in Chromium's OSCrypt-encrypted store.
6. Gaps:
   - Chromium's `CSVPassword` and `CredentialUIEntry` copies, and the reply `base::Value`s, are not wiped.
   - Tokens expire after 10 minutes, wiped by a delayed task even if no further import call arrives. They are also discarded on `forgetUnlockedKeys`, on `discardPasswords`, when the window closes and when the app terminates. A job cancelled in time never reaches the vault. When an abort races a finished job, the adapter discards the late token.
7. Nothing is logged.
8. CSV passwords stay native too.
   - `importPasswordsCSV` parses the file natively, stores the rows under a window-bound token and replies with `{token, count}` and non-secret `{url, username}` entries.
   - The shared Settings › Passwords pane gets one-time handles in place of passwords. An importer-scoped adapter (`importedPasswordSave.ts`, used only by `Passwords.tsx`) turns its first `savePassword` with a handle into `writePasswords` for the pane's own profile, and reports success for exactly `added` rows, so the pane's count is the number of new passwords.
   - `ReplyLeaksSecret` refuses any import or CSV reply that carries a non-empty `password` field, a non-empty `credentials` list or an exact password string.
9. `importBookmarksHTML` and `importPasswordsCSV` read only a path the native chooser returned to the same window, within 10 minutes, once.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_import test_downloads` (in `engine/ardiacore-linux/tests`) | 24 passed: 20 import tests and 4 GN/source-list tests, under ASan/UBSan with system SQLite and OpenSSL. Review round 1 added coverage for chunk bounds and added/existing counts, CSV summaries, the leak gate, vault expiry, `v12` and the empty-key fallback, FIFO refusal without blocking, Ardia's own data directory, and literal `<` in titles. |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed (before review round 1; unchanged since) |
| `pnpm --filter @ardia/linux test:host` | 352 passed after rebasing on `c525fb61`; 14 of them are in `hostImport.test.mjs` |
| `pnpm --filter @ardia/linux typecheck`, `build`, `test:graph` | Pass (graph: 3 tests). The graph test requires `state/import/apply.ts` to load only through `importApply.ts`, and `Passwords.tsx` to save through `importedPasswordSave.ts`. |
| `pnpm --filter @ardia/linux acceptance:self-test` | 60 passed |
| `clang++ -fsyntax-only` with the 156 tree's flags | `ac_linux_import_formats.cc` and `ac_linux_import_readers.cc` compile with no warnings. `ac_linux_import_service.cc` and the host hunks could not be checked: the 156 output tree is mid-build and lacks its generated mojom headers. |
| Cross-check | The native reader run over the acceptance generator's scratch profiles returns the counts the F12 scenario expects. |

## Needs native runtime acceptance on a Chromium 156 build

None of this has run inside Ardia, because I can't build Chromium in this lane. Run `pnpm --filter @ardia/linux acceptance -- <ardia> apps/linux/dist --only F12`, then the manual steps in `acceptance-suite.md` (#22 F12). Specifically:

1. The first compile of `ac_linux_import_service.cc` and the host hunks.
2. `PasswordImporter` writing into the basic store, the duplicate import adding nothing, and an isolated target landing in `Profile work`.
3. Chooser behaviour headless and on a desktop (portal and GTK).
4. Cancellation timing on a large history.
5. The import window end to end.

## Out of scope, tracked

- [#109](https://github.com/mckamyk/ardia/issues/109): keyring (`v11`) Chromium passwords
- [#110](https://github.com/mckamyk/ardia/issues/110): Firefox passwords (NSS `key4.db`)
- [#111](https://github.com/mckamyk/ardia/issues/111): tabs, cookies, addresses and cards

All three are implemented at source level in [U-import-handoff.md](U-import-handoff.md) (#158).

Safari and Apple automation are not applicable on Linux.

## Risks

- Writes and imports finish by event. The adapter gives up after 120 s without an event; each 500-password chunk emits progress.
- Pool tasks are `SKIP_ON_SHUTDOWN`. On app termination every job's cancel flag is set, so a running read stops at its next check (every 128 rows, and between files).

## Real profile schemas under Chromium's SQLite (#241)

`tests/test_import_real_schemas.py` builds the readers against Chromium 156's SQLite amalgamation (from `~/ardia-linux-build-156`, or `ARDIA_CHROMIUM_SRC`) with the flags in `third_party/sqlite/*.gni`, except ICU and the recover extension, which don't change SQL. It skips without that tree. `tests/fixtures/real-profiles` holds the CREATE statements of profiles that Chromium 155, Firefox 159 and Firefox 153.6 ESR wrote in scratch directories, and the `recovery.jsonlz4` each Firefox wrote for two local tabs. To refresh them, run each browser headless with a scratch profile (`chromium --headless=new --user-data-dir=…`, `firefox --headless --profile …`) and dump `sqlite_master` plus `meta` (Chromium) or `PRAGMA user_version` (Firefox).
