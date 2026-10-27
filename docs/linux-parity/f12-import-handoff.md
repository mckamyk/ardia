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
    - `Login Data` plus `Login Data For Account`. `v10` (basic) values are decrypted; `v11` (keyring) values and failures are counted and reported. Duplicates are removed.
    - Firefox `places.sqlite` bookmarks and history.
    - Web Store extensions from `Preferences` and `Secure Preferences`.
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
| `writePasswords` | `token, engineProfile` | `{imported, conflicts, invalid}`; errors `expired`, `ERR_PROFILE_PRIVATE`, `ERR_PROFILE_UNAVAILABLE`, `ERR_PROFILE_INVALID`, `ERR_PASSWORDS_UNAVAILABLE`, `ERR_PASSWORDS_IMPORT` |
| `discardPasswords` | `token` | boolean |
| `forgetUnlockedKeys` | — | discards the window's tokens |
| `chooseImportFile` | `requestId, "bookmarksHTML" \| "passwordsCSV"` | `true`, then `{type:"chosen", path \| null}`; errors `ERR_FILE_CHOOSER_UNAVAILABLE`, `ERR_FILE_CHOOSER_DISABLED`, `busy`, `unsupported` |
| `importBookmarksHTML` | `path` (chosen) | `{root, links}`; errors `ERR_FILE_NOT_CHOSEN`, `empty`, `tooLarge`, `unreadable` |
| `importPasswordsCSV` | `path` (chosen) | `{credentials, rows, duplicates, invalid}`; same errors plus `unsupported` |
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
   - Tokens expire after 10 minutes. They are discarded on `forgetUnlockedKeys`, on `discardPasswords` and when the window closes. Cancelled or abandoned jobs never reach the vault.
7. Nothing is logged.
8. CSV rows from the Settings › Passwords pane only leave the host when the bootstrap advertises `ArdiaPasswords.save`, the typed secret path #17 is building. Without it, the import fails with `ERR_PASSWORDS_UNAVAILABLE` before the file is read. [#113](https://github.com/mckamyk/ardia/issues/113) tracks this.
9. `importBookmarksHTML` and `importPasswordsCSV` read only a path the native chooser returned to the same window, within 10 minutes, once.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest engine/ardiacore-linux/tests/test_import.py` | 15 passed, run under ASan/UBSan with system SQLite and OpenSSL. Coverage: units, nested HTML, HTML limits, CSV variants, `profiles.ini`, discovery, a Chromium import with WAL snapshots and source hashes, history limit and since, partial errors, traversal, cancellation with temp cleanup, Firefox import, extensions, the stale-copy sweep, and GN/no-comment rules. |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | 303 passed on `linux` before #62, #63 and #68 were merged. After rebasing, `test_import.py` passes (15); the full rerun was still in progress at wrap-up with no failures so far. |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed |
| `pnpm --filter @ardia/linux test:host` | 329 passed after rebasing on #62, #63 and #68, 13 of them new in `hostImport.test.mjs` |
| `pnpm --filter @ardia/linux typecheck`, `build`, `test:graph` | Pass (graph: 3 tests). The graph test now requires `state/import/apply.ts` to be loaded only by `importApply.ts`. |
| `pnpm --filter @ardia/linux acceptance:self-test` | 50 passed, 3 of them new fixture tests |
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
- [#113](https://github.com/mckamyk/ardia/issues/113): CSV import in Settings › Passwords

Safari and Apple automation are not applicable on Linux.

## Risks

- `writePasswords` is a single host request. A very large password set on a cold store must finish within the 30 s request timeout. If it doesn't, the write still completes but the UI counts it as failed.
- Pool tasks are `SKIP_ON_SHUTDOWN`. Quitting during an import waits for the running read to finish.
