# Lane S handoff: credential and content-blocker follow-ups

Branch `t3/linux-security-followups`, rebased onto `linux` at `6bf5097b` (after #128, #130, #131 and #136). Source work for the F07 and F06 follow-ups #102–#108, tracked by #139. Runtime acceptance stays with those issues, #16, #17 and the #81 suite.

I couldn't build or run Chromium 156 here, so none of this has run as a browser. The changed native files do compile with `clang++ -fsyntax-only -Werror` against the real 156 headers, using the flags in the existing Linux out dir's `.ninja` files (read only; nothing built or written there). That check also found two errors #122 had left in the site-controls code, fixed here.

The secret paths, one by one, are in [f07-security-notes.md](f07-security-notes.md), updated for this lane.

## What was done

### #102 Which key encrypts new secrets

- **Detection.** os_crypt_async prefixes every ciphertext with its provider's tag: `v10` (Chromium's built-in key), `v11` (Secret Service or KWallet), `v12` (secret portal). The encryption probe in `ac_linux_credential_service.cc` encrypts a fixed non-secret string and classifies the tag (`ProtectionFromCiphertext`, `DescribeStorage` in `ac_linux_credentials.{h,cc}`).
- **Contract.** `ArdiaPasswords.status` and `ArdiaAutofill.status` add:
  - `protection`: `keyring` | `portal` | `obfuscated` | `none` | `unknown`;
  - `protectionFallback`: true when `--password-store` isn't `basic` but new secrets still get the v10 key (Chromium's silent fallback);
  - `storage`: a label such as "the Secret Service keyring", "KWallet" or "Chromium's built-in key, which only obfuscates".
- **Availability unchanged.** An obfuscated store still lists and reveals; it never looks like an empty vault.
- **UI.** `CredentialNotice` (Linux, above both panes) now titles each case ("Passwords aren't encrypted with your keyring", "System authentication is unavailable", …), warns when the keyring didn't answer, and always shows a "Storage on this computer" row naming the key and that showing secrets asks for the system password (polkit). Helpers: `credentialNotice`, `credentialNoticeTitle` and `credentialStorage` in `hostCredentials.ts`.

### #103 Wiping the engine's secret reply (shared engine, both platforms)

- **`Reply::SendSecret`** (`ac_engine.{h,cc}`) serializes through **`SendSecretJson`** (`ac_sensitive_args.{h,cc}`):
  - it reserves a buffer for the worst-case escaping (six bytes per input byte), so `JSONWriter` never reallocates and leaves no partial copy;
  - it runs the reply callback;
  - it then cleanses the buffer and the source value.
- **Callers.** `ac_passwords_reveal` and `ac_autofill_card_number` reply through it. Reveal also wipes the `CredentialUIEntry`'s UTF-16 password; the card reply wipes its UTF-16 and UTF-8 temporaries, and `Digits` reserves up front.
- **macOS.** The JSON a macOS caller receives is byte-for-byte the same. This touches shared engine code, so the next macOS build should confirm it compiles there; I couldn't build for macOS.

### #104 Linux wording in the shared panes

- **Transform.** `apps/linux/credentialWording.ts` is a Linux-only Vite transform, registered in `vite.config.ts`. It replaces exact strings in the shared `Passwords.tsx`, `Autofill.tsx` and `privacyRows.ts`; the shared files are unchanged.
- **Replaced wording.** Mac, Keychain and Touch ID become the system keyring and the system password. The Mac-app note for password managers now says Ardia doesn't connect them to desktop apps on Linux yet; that work is #100.
- **Drift guard.** A replacement whose source string disappears throws, so a later copy change fails the Linux build instead of shipping macOS wording.

### #105 Installing the polkit policy

- **Installer.** `scripts/linux/install.sh` ships inside the package as `ardia/install.sh` (`package.py`).
  - Options: `--prefix` (absolute; letters, digits and `._/+-` only; default `/usr`) and `--destdir`.
  - It installs `lib/ardia`, an absolute `bin/ardia` launcher, the desktop entry and the icon under the prefix.
  - It always installs `dev.ardia.linux.policy`, mode 0644, at `DESTDIR/usr/share/polkit-1/actions`, the only directory polkit reads.
- **Arch.** `scripts/linux/arch/PKGBUILD` (`ardia-bin`, depends on `polkit`) packages the release tarball through that installer.
- **Fail closed.** The policy-missing message now names the directory and how to install it.

### #106 Navigation hold

- **Hook.** `engine/patches/chromium-linux-navigation-hold-hook.patch` is Linux-only, registered after `chromium-linux-page-agent.patch`, and applies with zero fuzz to the 156 source. It adds `g_ardia_linux_navigation_throttles`, called at the end of `ChromeContentBrowserClient::CreateThrottlesForNavigation`.
- **Throttle.** `ac_linux_navigation_hold.{h,cc}` installs the hook (from `StartLinuxSiteControls`) and defers `WillStartRequest` for every navigation in a held profile and its private child, except the `chrome-extension`, `chrome`, `chrome-untrusted`, `devtools` and `about` schemes. That keeps the shell, Settings and the bridge page itself running.
- **When it holds.** It holds:
  - the app profile only when `IsNewProfile()` (first launch);
  - every isolated profile on its first load in a run.
- **When it ends.** The hold ends:
  - when uBlock answers through the bridge page (the bridge waits for `isReadyPromise`), or for an isolated profile, when its first settings copy finishes;
  - when the blocker is missing or fails to load;
  - when the profile goes away;
  - after 10 s.

  Held navigations resume through weak pointers, and an early end logs a warning.

### #107 Restricted-component hook in every process

#122 already assigns `g_ardia_restricted_component_extension` first thing in `ChromeMainDelegate::BasicStartupComplete` under `IS_LINUX`. This lane verified against the 156 sources that this covers every process:

- **Every process.** `ContentMainRunnerImpl::Initialize` calls `BasicStartupComplete` for every process type before `Run` dispatches to the browser, the zygote (whose forked renderers inherit the global) or any other process.
- **The renderer path.** The renderer's content-script decision goes through the patched check: `ExtensionInjectionHost` → `GetContentScriptAccess` → `CanRunOnPage` → `IsRestrictedUrl`.

`test_site_controls.py` asserts that chain when the 156 tree is present. The S107 scenario checks it at runtime.

### #108 Content blocker in isolated profiles

- **Load.** `ShellHostView::Initialize` now calls `StartLinuxContentBlocker(profile(), page_profile())`. The app profile's blocker leads. A different regular page profile gets its own blocker, which follows the leader through a `WeakPtr` to the leader's blocker.
- **Bridge.** It has two new fixed operations:
  - `settings` reads the selected lists, the full whitelist (site exceptions and the off directive) and auto-update;
  - `follow` applies them and writes only what differs.

  Neither is reachable from a `contentBlocker.*` call. Copied settings are validated before they reach the page: list ids, no CR, LF or NUL in directives, at most 512 lists and 10,000 directives of 2 KiB.
- **First load.** A follower copies the leader's settings, retrying up to six times with 1, 2, 4, 8 and 16 s backoff, and that first copy ends its hold.
- **Mirroring.** After each successful mutation on the leader, its settings are copied to every loaded follower.
- **Calls from isolated windows.** `contentBlocker.*` calls from an isolated window now act on the leader's settings, as on macOS, instead of answering "not available in this profile yet".

### Fixes found on the way

- **156 compile errors from #122.** `ac_linux_site_controls.cc` bound references to `DictValue`'s temporary pairs, an error under 156's `-Wrange-loop-bind-reference`. The service also lacked `#include "base/one_shot_event.h"`.
- **Stub fidelity.** The shared `base::DictValue` test stub (`test_session_merge.py`) iterated its `std::map` directly, which hid the first error. It now yields Chromium's proxy pairs, and the full native suite still passes.
- **Profile observers.** The site-controls stub `Profile` now skips observers removed during destruction, as Chromium's `ObserverList` does.

## Tests run

All on this machine after the rebase. Every fix had its failing test first; each native fix was also checked by mutation, and the test failed with the fix removed.

- **`python3 -m unittest discover -s engine/ardiacore-linux/tests`:** 417/417.
  - `test_credentials.py`: provider tags and storage labels, and the probe and status wiring.
  - `test_site_controls.py`: 12 tests.
    - Pure follow and hold policy.
    - Service fixtures for the hold: deferral by scheme and private child, release on ready, the 10 s deadline, a missing blocker and profile teardown.
    - Service fixtures for following: the first copy, retries, give-up after six attempts, mirroring after mutations, calls from isolated and private windows, and the leader destroyed with a follower's retry pending.
    - Patch registration and a zero-fuzz dry run on the 156 source, and the #107 process chain.
  - `content_blocker_bridge.test.mjs`: 10, including `settings` and `follow`.
- **`python3 -m unittest discover -s engine/chromium/src/chrome/browser/ardia/tests`:** 16/16. The new `test_secret_reply.py`:
  - compiles the real `ac_sensitive_args.cc`;
  - replaces `operator new`/`delete` to scan every freed block for the secret;
  - shows that a plain serialization leaves the secret behind, while `SendSecretJson` leaves only the receiver's own copy, including with 2,000 control characters and 3,000 quotes and backslashes;
  - fails if the cleanse or the reserve is removed.
- **`python3 -m unittest discover -s engine/patches/build/tests`:** 23/23. Covers `install.sh` for three prefixes, the refused prefixes, the PKGBUILD's `package()` run in bash, and `makepkg --printsrcinfo`.
- **`pnpm --filter @ardia/linux typecheck`:** clean.
- **`pnpm --filter @ardia/linux test:host`:** 367/367, including `credentialWording.test.mjs` (3) and the new notice test in `hostCredentials.test.mjs`.
- **`pnpm --filter @ardia/linux build`:** OK. The bundle has none of the replaced macOS strings, and it has "Storage on this computer".
- **`pnpm --filter @ardia/linux test:graph`:** 3/3.
- **`pnpm --filter @ardia/linux acceptance:self-test`:** 63/63, with the catalogue including the six new scenarios.
- **`node --test engine/ardiacore-linux/tests/bootstrap.test.mjs`:** 8/8.
- **Syntax check against the 156 headers:** `ac_engine.cc`, `ac_sensitive_args.cc`, `ac_passwords.cc`, `ac_autofill.cc`, `ac_linux_credentials.cc`, `ac_linux_credential_service.cc`, `ac_linux_site_controls.cc`, `ac_linux_site_controls_service.cc` and `ac_linux_navigation_hold.cc` all pass.
- **Not run.** Nothing under `apps/browser` changed, so the shared browser suite wasn't run.

## Needs native runtime acceptance on a Chromium 156 build

New scenarios are in `apps/linux/scripts/acceptance/scenarios/s10*.mjs`, with their rows in [acceptance-suite.md](acceptance-suite.md). Run them with `--only S102,S104,S105,S106,S107,S108`; S106–S108 need `ARDIA_ACCEPTANCE_CONTENT_BLOCKER`.

- **S102.** A `gnome-libsecret` store without a session bus reports the v10 fallback; `basic` doesn't.
- **S104.** Settings › Passwords and Autofill have no macOS wording.
- **S105.** The packaged `install.sh` places the policy, `pkaction` lists it when it's installed system-wide, and a non-background status names the missing policy.
- **S106.** A fresh profile restored onto the blocker fixture never requests the ad.
- **S107.** uBlock's `tabs.executeScript` into `chrome://version` is refused, and the shell has no component context.
- **S108.** An isolated window blocks the ad and follows exceptions set from either window.
- **Also check on that build:**
  - the new patch applies in the full series;
  - `ac_linux_navigation_hold.cc` links;
  - the hold doesn't delay a restored session noticeably on an existing profile (it shouldn't hold at all there).
- **Manual:**
  - a working Secret Service or KWallet reported as `keyring`;
  - a root install with `pkaction` and the F07 polkit prompt checks;
  - the panes' layout.
- **macOS.** The macOS build should compile the #103 engine change.

## Issues

- **Closes:** #139 (tracking) and #103 (fixture-verified; it has no runtime scenario).
- **Part of:** #102, #104, #105, #106, #107 and #108 (each has an automated scenario, so it stays open until a 156 run passes), #16 and #17.
- **Follow-ups opened:**
  - #151: macOS wording elsewhere in shared copy (onboarding, Appearance, Privacy, Engines, import).
  - #152: publishing a Linux release tarball and an AUR package.
- **Existing issues this relies on:** #100 (native messaging for password managers) and #147 (Sync pane wording).

## Open risks

- **The hold delays first pages.** It delays the first pages of a fresh profile and of each isolated profile's first window by up to uBlock's start time (capped at 10 s). Session-restored isolated windows are held too; macOS skips holds during launch.
- **Large settings copies.** A leader whose settings reply exceeds the bridge's 1 MiB reply cap (thousands of long exceptions) can't be copied. The follower retries and then logs a warning.
- **Probe ciphertext.** The protection probe encrypts a constant on the UI thread whenever status runs. It's cheap AES, but a Helium patch that changed the provider tags would make status report `unknown`.
