# #256, #261: a keyring locked at launch

Issues: [#256](https://github.com/mckamyk/ardia/issues/256) (Live folders) and [#261](https://github.com/mckamyk/ardia/issues/261) (Sync), found by the desktop-session harness (#240, F11K and F13K). Branch `fix2-keyring-locked`.

## What the harness saw, and what a desktop sees

The harness's private session bus has no service files, so gnome-keyring can't start its unlock prompter (`/usr/lib/gcr-prompter`, `org.gnome.keyring.SystemPrompter`). On a desktop, reading an item from a locked keyring makes gnome-keyring show "Unlock Login Keyring". The harness sees what a user sees after dismissing that prompt.

libsecret reports a dismissed prompt badly:

- `secret_password_lookup_sync` finds the item, asks to unlock it, and when the prompt is dismissed returns `NULL` with no error. That's the same answer as "no such item".
- `secret_password_store_sync` into a locked collection fails with `secret-error` 2 (`IS_LOCKED`), "Cannot create an item in a locked collection".
- `secret_service_get_sync` succeeds whether or not the keyring is locked.

## Cause

`SecretServiceKeyStore::Load` (`ac_linux_sync_keystore.cc`) treated `NULL` as `kNotFound`, and `Probe` only checked that the service answers. So:

- Live: `LiveCore::WithCredential` cached the token as `kAbsent` and answered `ERR_LIVE_NOT_CONNECTED`. The renderer reads that as `signedOut` because Settings still lists the account, so the folder said "Sign back in to GitHub". `kAbsent` is cached for the process, so after the keyring was unlocked Refresh never read the keyring again and sent no request.
- Sync: `Unlock` got `kNotFound`, `status` said `keyStoreStatus: "ok"`, and the pane showed the plain "Enter your recovery phrase" line instead of the keyring message. `EnterPhrase` then tried to save the key, got `kLocked`, and refused with `keychain`.

## Fix

### Keystore (`ac_linux_sync_keystore.cc`)

- `Load`: when the lookup returns `NULL` without an error, a `secret_password_search_sync` with `SECRET_SEARCH_ALL` and no `UNLOCK` flag looks for a matching item. That search never prompts. A match means the item exists but stayed locked, so `Load` returns `kLocked`. No match means `kNotFound`, and a search error is classified like any other.
- `Probe`: also reads the `default` alias collection's `Locked` property (`secret_collection_for_alias_sync`, `secret_collection_get_locked`). `status` reports `keyStoreStatus: "locked"` while it is locked. Probe never prompts.
- Every new symbol is optional, so an older libsecret behaves as before.

### Live folders

The native Live state machine already did the right thing for `kLocked`. It just never received it. Per source:

| Cache state | Meaning | Next request |
| --- | --- | --- |
| `kUnknown` | not read yet, or the last read failed (locked, unavailable) | reads the keyring (desktop: may prompt) |
| `kLoading` | a read is in flight | waits for it |
| `kPresent` | token in memory | uses it; no keyring access, so locking later doesn't matter |
| `kAbsent` | the keyring answered: no token | `ERR_LIVE_NOT_CONNECTED` |

A locked read leaves `kUnknown` and answers `ERR_LIVE_KEYRING_LOCKED`. So the next Refresh after an unlock (from the prompt, Seahorse or anywhere else) reads the token, with no restart. `Commit` refuses to save over a token it couldn't read, and `Disconnect` is the only path that deletes one. A locked keyring therefore never deletes or replaces a stored token.

Renderer side:

- `hostLive.ts` maps `ERR_LIVE_KEYRING_LOCKED` to `keyringLocked` and `ERR_LIVE_KEYRING_UNAVAILABLE` to `keyringUnavailable`. Before this, both were `other`, which reads as "We're having trouble connecting".
- `linuxWording.ts` adds those two cases to the shared `errorText` at build time: "Unlock your keyring to refresh GitHub" and "Start your keyring to refresh GitHub". The shared source and macOS are unchanged.

### Sync (`ac_linux_sync.cc`, `apps/linux/src/hostSync.ts`, `syncEngine.ts`)

| Native state | Set when | Effect |
| --- | --- | --- |
| `unreadable_account_` | `unlock(account)` failed with `kLocked` or `kUnavailable` | this device's key exists somewhere Ardia can't reach yet |
| keys in memory, `unsaved_account_` set | `enterPhrase(account, words)` for `unreadable_account_` matched the folder's chain but the save failed (locked or unavailable) | sync runs; the key is in process memory only |
| `unsaved_account_` cleared | a later call, at most every 30 s, finds `WritesWithoutPrompt()` and the save succeeds | the keyring holds the key again |
| `key_account_` | any key is in memory (created, entered, unlocked or resumed) | `unlock(key_account_)` and `enterPhrase(key_account_, "")` answer from memory, never touching the keyring |

- `enterPhrase` for any other device id still refuses with `keychain` when the key can't be saved, so Turn On Sync and joining from another device still write nothing (F13K's refusal checks).
- The deferred save only runs when `WritesWithoutPrompt()` saw an unlocked `default` collection. A missing alias, a locked collection or an older libsecret without the collection calls all mean "don't try", because a save into those would prompt. That is stricter than `Probe`, which reports `ok` when it can't tell. Nothing is written outside the keyring. The file key store is only used for background test instances with an explicit data dir, as before.
- `enterPhrase(account, "", folder)` means "resume from the keyring". For `key_account_` it checks that the key in memory opens the folder's chain. For `unreadable_account_` it re-reads the key, checks the chain, and then behaves like an entered phrase. It never accepts a new key.
- Failover: the window running sync can close after the phrase was entered while the keyring is still locked. The next window's `startSync` calls `unlock(device)`, and that returns `true` from memory, so it never shows "locked".
- Renderer: `watchLockedSync` runs a 3 s `ArdiaSync.status` poll (`keyringResumer`) only while sync is on and locked, in the window running sync. It is torn down when that window loses the role or unloads. Each reply refreshes the pane's keyring message. Once `keyStoreStatus` is `ok` and `lastKeyStatus` is still `locked` or `unavailable`, it calls the shared engine's `enterRecoveryPhrase(folder, "")`, which runs its normal locked-resume path (load state, publish, sync). If the key turns out to be missing, `lastKeyStatus` becomes `notFound` and polling stops trying. The pane then shows the plain recovery-phrase line. It also resumes once whenever `status.unlocked` is true while the pane is locked, because the core already holds the key. That covers a held key saved by the 30 s retry before the poll saw the keyring open.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_keyring_locked` | 1 test (2 node cases): a private dbus-daemon whose only service dir registers `gcr-prompter`, a private Xvfb for its dialog (answered with xdotool), and a private gnome-keyring. Live: connect, lock, launch; dismiss the prompt (`keyringLocked`, no request, token still stored), then unlock through the prompt (Refresh works), then dismiss again and unlock from outside (Refresh works with no prompt); the stored token is byte-identical. Sync: the locked launch is named, the phrase resumes with the key in memory, an outside unlock saves it, `""` resumes after an outside unlock, unlocking through the prompt at launch or at the phrase works, and other device ids are refused. Both cases fail on `linux` (`signedOut`; `keyStoreStatus` `ok`). |
| `python3 -m unittest test_sync test_live` | 3 + 6 pass. `sync_scenarios` "held key" (from the #293 review's repro): a second window's `unlock` answers from memory, and the deferred save waits for a confirmed-open default collection. It fails before the review fixes. |
| `pnpm --filter @ardia/linux typecheck`, `test:host`, `build` | ok, 534 pass, ok |
| `bash scripts/linux/syntax-check-156.sh ac_linux_sync_keystore.cc ac_linux_sync.cc ac_linux_sync_service.cc ac_linux_live.cc ac_linux_live_service.cc` | 5 of 5 |

## Needs the next build to confirm

The fix is native, so the ee23b331 engine still reads the locked item as absent. With this branch's UI on ee23b331, F11K and F13K fail the same locked-at-launch checks as before and pass the rest. On the next 156 package:

- F11K: "with the keyring locked at launch, Refresh says to unlock the keyring" and "after unlocking the keyring, Refresh updates the folder without signing in again" pass.
- F13K: "names the locked keyring" and "entering the recovery phrase resumes sync" pass.
- On a desktop: lock the login keyring and relaunch. Refresh a Live folder and dismiss the unlock prompt: the folder says "Unlock your keyring to refresh GitHub". Unlock the keyring in Seahorse and Refresh: it updates. Sync: the pane says "Your keyring is locked…". Unlock in Seahorse and sync resumes within a few seconds, with no restart.
