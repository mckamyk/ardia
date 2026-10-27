# Lane P handoff: encrypted folder sync on Linux (#23 F13, source work #138)

Linux now has an `ArdiaSync` native module. The shared sync engine runs on it in exactly one renderer, and Settings › Sync is no longer "unavailable" on Linux. The folder format is the macOS one, byte for byte, so Linux and macOS devices can share a folder. All of this was built and tested from source only. None of it has run in a Chromium 156 build yet.

## What was built

### Native core (plain C++, fixture-tested)

These files build against BoringSSL in Chromium and against system OpenSSL in the fixtures.

| File | What it does |
| --- | --- |
| `engine/ardiacore-linux/src/ardia/linux/ac_linux_sync_crypto.{h,cc}`, `ac_linux_sync_wordlist.cc` | The port of `SyncCrypto.swift` and `RecoveryPhrase.swift`. |
| `ac_linux_sync_vault.{h,cc}` | The folder transport (the `SyncVault.swift` equivalent). |
| `ac_linux_sync_json.{h,cc}` | A small JSON DOM for the legacy `pw:` password rewrite (`LegacyPasswords` in `SyncModule.swift`) and for native replies. |
| `ac_linux_sync_keystore.{h,cc}` | Key storage. |
| `ac_linux_sync.{h,cc}` | `SyncCore`: method table, argument checks, key state, the authority lease and the disclosure window. |

**Crypto and the recovery phrase**
- HKDF-SHA256 with salt `netnyahoo-sync/v1`, which derives `file-key`, `name-key`, `password-key` and `password-nonce-key`.
- Chain and scope tags are base32 HMAC tags.
- Each file is `NNS1 | nonce | AES-256-GCM | tag`. The AAD is `NNS1<chain>/<scope>/<file>`, and the plaintext is a u32 length prefix with padding to 1 KiB.
- Passwords use version byte 1, an HMAC-derived nonce and 32-byte padding.
- The phrase is BIP-39: 24 words, an 8-bit checksum, and the same lenient input as macOS (case, punctuation, kit numbering, 4-letter prefixes).

**Folder transport**
- Files are named `<chain>/<scope>/<random 128-bit id>.nns`.
- A write goes to `.ardia-staging-<random>` in the same directory (`O_EXCL|O_NOFOLLOW`, mode 0600), is fsynced, then moved into place with `renameat2(RENAME_NOREPLACE)`. On filesystems without that rename it falls back to `link`, then to a checked `rename`. The directory is fsynced afterwards.
- Staging files older than an hour are removed on the next write. Readers ignore them; macOS's reader does too.
- Reads skip anything that isn't `<tag>.nns`, so Dropbox and Syncthing conflict copies are never applied.
- Syncthing's `.syncthing.<id>.nns.tmp` is reported as pending when the whole file isn't there yet.
- Half-copied, empty, renamed or moved files are reported as damaged, with their age.
- Only the sync folder the user chose may be a symlink. Below it, the chain and scope folders and every file are opened relative to open directory descriptors with `O_NOFOLLOW` (`openat`, `mkdirat`, `renameat2`, `unlinkat`).
  - A symlinked chain or scope reads as missing: `chainStatus` says `missing`, reads come back empty, and writes and removes refuse.
  - Deleting a chain walks open directory descriptors and removes entries with `unlinkat` (`AT_REMOVEDIR` for folders). A folder swapped for a link mid-walk is unlinked as a link, so nothing outside the chain can be deleted.
  - Ids from JS must be tags, so `../` can't escape.

**Legacy `pw:` passwords**
- `read` and `openLocal` seal a legacy record's `p` into `s` before JS sees it, and drop `pw:` base strings.
- `write` turns `s` back into `p`, so old builds keep their records.
- If a document with `pw:` can't be parsed or sealed, a folder file reads as damaged. A local state file reads as `null`, and `sealLocal` and `write` then refuse for the session with `unreadable`, as on macOS. Lone surrogate escapes become U+FFFD.

**Key storage**
- Normally the key lives in the Secret Service through libsecret, loaded at runtime from `libsecret-1.so.0`. The item uses schema `dev.ardia.linux.SyncKey`, label "Ardia Sync Key", the account attribute set to the device id, and the default collection. The 32 bytes are stored as hex.
- No D-Bus, no Secret Service or a missing libsecret reports `unavailable`. `SECRET_ERROR_IS_LOCKED` and a dismissed unlock prompt (`G_IO_ERROR_CANCELLED`) report `locked`. Nothing falls back to a file in normal use.
- Only a hidden test instance (`ARDIA_BACKGROUND=1`) started with an explicit `--user-data-dir` that isn't Chromium's default (`UseFileKeyStore`) uses a 0600 file in `<user-data-dir>/Ardia Sync Keys/sync-key-<device id>`. That is how macOS test instances work too.
- The file store refuses a key file that isn't a regular file owned by the user with mode 0600 or tighter.

### Chromium glue (not compiled here)

`ac_linux_sync_service.{h,cc}`:
- One process-wide `SyncCore` runs on a `MayBlock` sequenced task runner (`CONTINUE_ON_SHUTDOWN`, so a pending keyring prompt can't hold up shutdown; writes are atomic anyway).
- The lease and the disclosure window live on the UI thread.

Choosing, disclosing and opening:
- Choosing a folder and saving the kit use `SelectFileDialog` with `ChromeSelectFilePolicy`, parented to the asking window.
- Disclosure is gated by `ardia::Reauth` (#118's polkit). A grant lasts 60 s, and `forget`, `createPhrase` and `enterPhrase` revoke it.
- The QR code comes from `qr_code_generator::GenerateBitmap` as a PNG data URL.
- Copy Words writes the words with `ScopedClipboardWriter`, adds `x-kde-passwordManagerHint: secret` as a raw clipboard format (`PhraseClipboard`; on Linux the macOS counterpart is ConcealedType), and calls `MarkAsConfidential()`. On 156 Ozone ignores `MarkAsConfidential()`, so the hint is what tells clipboard managers (Klipper, CopyQ, cliphist with its filter) not to save the words.
- Show Sync Folder uses `platform_util::OpenItem(OPEN_FOLDER)`.

**Defaults**
- The default folder is `--ardia-sync-folder` if given, otherwise `$HOME/Ardia Sync`.
- The bootstrap gives the sync calls longer deadlines: 180 s for key and disclosure calls, 120 s for folder reads, writes and deletes, and 900 s for the pickers.

**Registration in `ac_linux_host.cc`**
- An include.
- `AppendLinuxSyncCapabilities(capabilities)` in `Bootstrap()`.
- In `Invoke()`, `LinuxSyncInvoke({"<host id>#<renderer generation>", private window, utility}, profile(), shell_, …)`.

`BUILD.gn` adds the files plus `//base:i18n`, `//components/device_reauth`, `//components/qr_code_generator:bitmap_generator` and `//third_party/boringssl`.

### JS (apps/linux)

**`hostSync.ts`** is the `ArdiaSync` adapter, registered in `hostModules.ts`.
- `newDeviceId` (16 random bytes in base32) and `deviceName` (cached from `ArdiaSync.deviceName`) answer locally, because the engine calls them synchronously.
- `readPasswords` returns `null` and `savePassword` returns `false` (#145).
- Everything else becomes `host.invoke("ArdiaSync.<method>", {args})`; nothing goes through `engine.call`.
- It also holds the keyring messages that replace the engine's "Keychain" text.

**`syncAuthority.ts`** keeps one app-level sync authority (the F27 service-authority pattern).
- Every non-private browser window competes for the Web Lock `ardia.linux.sync-authority`. Settings and Import never compete.
- The lock holder then claims the native lease (`ArdiaSync.claimAuthority`, renewed every 10 s, expiring after 30 s). It retries every 5 s while a stale holder, such as the previous document of a reloaded window, still owns it. Then it runs the real shared engine (`startSync`).
- Other renderers mirror its `useSync` state over the BroadcastChannel `ardia.linux.sync`. They forward Turn On, Enter Phrase, Stop, Sync Now, the type and profile toggles, Add Profile, Adopt Theme and the chosen folder as calls. With no authority, a call fails within 10 s with a plain message. When the authority's window closes it announces `gone`, its lock passes on, and the next window takes over.

**`syncEngine.ts`** replaces `apps/browser/src/state/sync/engine.ts` through `vite.config.ts`, so the shared Sync pane, sheets, menu, telemetry and import keep their imports.
- Before starting, the authority re-reads `sync.json`, `sync-state.nns`, `sync-journal.nns` and every `sync-journal-N.nns` through the new `refreshDocument` (`hostDocuments.ts`, `shims/shellBase.tsx`). The bootstrap snapshot is capped at 128 documents and 8 MiB and can be stale after a failover. Without this refresh, a takeover could save an empty `sync.json` over the real one.
- The re-read probes every journal part up to 1000, in batches. Parts after the first missing one are stale (the journal numbers parts consecutively from 1), so they are deleted before the engine reads the journal.
- If starting fails (for example a document read error), the window keeps its Web Lock, releases the native lease and retries with backoff (5 s doubling, up to 60 s), instead of leaving sync with no owner.
- `main.tsx` starts sync in every renderer: as a candidate in browser windows, as a client in utility windows. It also exposes `ardiaLinux.sync` for the acceptance suite.

## The contract

Every method is a host invoke, `ArdiaSync.<method>` with `{ args: [...] }`. A failure is `{ code, message }`.

**Rules for every call**
- A private window gets `ERR_PRIVATE_SERVICE` for every method.
- Bad arguments get `ERR_SYNC_ARGUMENT`. Folders must be absolute paths of at most 4096 bytes, and account ids must be 26-character base32 tags.
- There are at most 8 arguments, nested at most 3 deep.
- Payloads are capped at 32 MiB, phrases at 4 KiB, known-id lists at 200k entries.

| Route | Methods | Rules |
| --- | --- | --- |
| Any renderer | `folderInfo(path\|null)`, `deviceName()`, `status()`, `chainStatus(folder)` | `folderInfo` returns `{path, defaultPath, iCloudAvailable, inICloud: false, exists, writable, hasSyncData}`. On Linux `iCloudAvailable` means the default folder's parent exists, because the shared pane uses it to gate the default folder. `status` returns `{keyStore, keyStoreStatus, lastKeyStatus, unlocked, unreadable, passwords: false}`. |
| Lease holder only | `createPhrase`, `enterPhrase`, `unlock`, `forget`, `createChain`, `write`, `read`, `remove`, `deleteChain`, `sealLocal`, `openLocal` | Anyone else gets `ERR_SYNC_NOT_AUTHORITY`. Replies match macOS: `PhraseResult`, the `Listing` with `damaged.age` in ms, sealed base64 strings, `"ok"\|"missing"\|"unavailable"`. Without a key they fail with `locked`. After an unreadable legacy state, `write` and `sealLocal` fail with `unreadable`. |
| Disclosure | `recoveryWords`, `qrCode`, `copyRecoveryPhrase` | `locked` if there's no key. Otherwise polkit asks (or a grant from the last 60 s is reused). A refusal returns `null`. |
| Pickers | `chooseFolder(current)`, `saveRecoveryKit("text")` | Cancel returns `null`. `saveRecoveryKit("pdf")` fails with `ERR_SYNC_UNSUPPORTED`. The kit asks for polkit after the path is chosen, writes atomically with mode 0600 and returns the path. |
| Lease | `claimAuthority()` → `{granted, leaseMs}`, `releaseAuthority()` | Utility windows are never granted. |
| Not on Linux | `recoveryKitPreview`, `copyRecoveryKit`, `shareRecoveryKit`, `readPasswords` → `null`; `savePassword` → `false` | Tracked in #147 and #145. |

## Tests run

| Command | Result |
| --- | --- |
| `pnpm --filter @ardia/linux typecheck` | Passes. |
| `pnpm --filter @ardia/linux test:host` | 413 of 413 pass, including 10 in `src/hostSync.test.mjs`. |
| `pnpm --filter @ardia/linux build` | Passes. The bundle routes the shared Sync pane through `syncEngine.ts`. |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | 458 of 458 pass, including 3 in `test_sync.py`. |
| `pnpm --filter @ardia/linux test:graph` | 3 of 3 pass. |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 8 of 8 pass. |
| `node --test scripts/acceptance/tests/*.test.mjs` (`acceptance:self-test`) | 65 of 65 pass, including 2 in `f13.test.mjs`. The stock-Chromium F09 check is flaky under load and passes on re-runs. |

What `test_sync.py` covers:

1. **The ASan/UBSan fixture `tests/fixtures/sync_scenarios.cc`:**
   - the wordlist SHA-256 and Trezor's four vectors;
   - lenient input, and the wordCount, unknownWord and checksum errors;
   - the macOS vectors: the chain and app scope tags for zero entropy, and opening the 0.2.32-sealed fixture from `packages/sync/tests/fixtures`;
   - padding; tampering at five offsets, truncation, and the wrong file, scope or phrase;
   - deterministic password sealing;
   - a folder with no plaintext and 0600 files; staging, truncated, empty, renamed and moved files; Syncthing temp files; conflict copies; a restart reading the same files; path-traversal removes; a chain delete that doesn't follow a symlink;
   - legacy `pw:` round trips and refusals; JSON depth and UTF-8 limits; 100k-key objects parse in under 3 s (0.67 s under ASan; duplicate keys keep the last value);
   - a delete race in which a hook swaps a folder for a link to a victim folder between listing and walking (the old path-based walk deleted the victim's file); symlinked chain and scope folders neither read, remove nor delete through the link;
   - the file key store (0600 file, 0700 folder, symlink and 0644/0640 files refused), the test-store gate, the keyring error mapping and the Copy Words clipboard formats;
   - every `SyncCore` method, including keychain failures, the unreadable latch and the 0600 kit;
   - lease and disclosure-window timing.
2. **Cross-format exchange.** `tests/sync_reference.mjs`, an independent `node:crypto` implementation of the spec, writes a folder and sealed passwords that the C++ opens and re-seals byte for byte. The C++ then writes a folder that the reference opens, checking it for plaintext.
3. **libsecret with no bus.** The real `libsecret-1.so.0` with no session bus reports `unavailable` for probe, load, save and delete. This never touches a real keyring.
4. **Capability list.** The native method list equals `SYNC_CAPABILITIES`.
5. **Two simulated devices.** `tests/sync_devices.test.mjs` runs two (then three) device processes on the real `SyncCore` and vault, sharing one temp folder, with the real `syncScope` merge from `packages/sync`:
   - setup and a numbered, upper-case join;
   - a stranger's phrase (mismatch), 23 words and swapped words are refused;
   - offline and concurrent edits converge on the newest;
   - a delete propagates;
   - a device restarts, is locked until it unlocks from its key file, and keeps syncing;
   - compaction runs, and a third device joins from logs plus a snapshot;
   - a tampered file is never applied and is counted as damaged;
   - the folder holds no plaintext, every file is padded, and no staging file is left behind.
6. **Source checks.** No logging in the sync sources, and the BUILD and host registration are in place.

I didn't run `pnpm -C apps/browser test`: no shared browser, demo or `packages/sync` file changed.

## Still needs native runtime acceptance on a Chromium 156 build

None of this was compiled or run in Chromium; the build belongs to another thread. Still unchecked:

- `ac_linux_sync_service.cc` and the host hunks compiling against 156: Reauth, the QR bitmap, the PNG codec, the clipboard, `OpenItem` and `SelectFileDialog` signatures.
- The F13 acceptance scenario (`apps/linux/scripts/acceptance/scenarios/f13-sync.mjs`): two scratch profiles through a temp folder, the single authority, the `ERR_SYNC_NOT_AUTHORITY` refusal, no plaintext, joining, bookmarks both ways, restart, stop.
- Its manual rows: the keyring item, a locked or absent keyring, polkit-gated disclosure, the folder picker, Linux with macOS through a real synced folder.
- Whether shared-store changes the authority makes (bookmarks through Chrome's BookmarkModel, settings through the Linux session merge) reach the other windows the way they do on macOS.

## Follow-ups

- **#174 macOS password vector.** Add a sealed-password test vector produced on a Mac.
- **Phrase tokenizing.** Non-ASCII phrase input splits on a fixed list of separator ranges rather than Swift's `CharacterSet.lowercaseLetters`. ASCII input behaves identically, and a non-ASCII token is always an unknown word on both, but the reported word can differ.
- **#145 Passwords.** No non-prompting sealed engine path exists for passwords yet, so they don't sync on Linux. The adapter skips them, and nothing is deleted.
- **#146 Document size.** Linux's 8 MiB document limit can reject a large `sync-state.nns`.
- **#147 Linux copy and kit formats.** The Sync pane still says Mac, iCloud and Finder, and the kit has no PDF, preview, copy or share.

## Risks

- **Phrase between windows.** Enter Recovery Phrase in Settings sends the typed words over a same-origin BroadcastChannel to the authority renderer. Both are trusted `chrome://ardia` renderers, and the words never reach a page or the disk.
- **Takeover delay.** After the authority renderer crashes, the next window waits up to 30 s for the native lease, and sync pauses for that long.
- **Non-UTF-8 folders.** A folder whose path isn't UTF-8 can't be chosen in the picker (it returns `null`).
