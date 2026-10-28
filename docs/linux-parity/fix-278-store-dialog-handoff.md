# #278: the Chrome Web Store's install dialog can be answered

Issue: [#278](https://github.com/mckamyk/ardia/issues/278), found by the desktop-session harness (#240, F09S). Branch `fix/278-store-dialog`.

## Cause

The Web Store bridge (#235) listens on every store tab with `evaluate(STORE_SCRIPT)`, which the Linux host maps to the native `webStoreRequest` command. That command is a long poll: it answers only when the store asks to remove an extension, the page changes or a newer wait supersedes it.

`HostTab.command` sends every command through the tab's ordered queue (`enqueueTab`), except `downloadFavicon`. So from the moment a listing loaded, every later command for that tab waited behind the long poll: `tab.viewport`, `focus`, `evaluate`. When Add to Ardia raised Ardia's dialog, the shell measured the page as covered and asked to hide it, but that `tab.viewport` call never reached the host. The page view stayed on top of the dialog, so the pointer's Cancel and Add Extension reached the page, the shell never got focus for Escape, and the store's install request stayed pending.

`extensions.installCrx` prompts have no store tab, so their page hid normally (F09C passed).

A harness probe confirmed it: an `evaluate("1+1")` on the store tab answered in 164 ms before Add to Ardia and timed out after it.

## Fix (Linux UI only)

`apps/linux/src/hostTab.ts`: `webStoreRequest` joins `downloadFavicon` in `UNORDERED_COMMANDS`, so the wait is sent directly and never holds the tab's queue. The native wait keeps its own rules (one waiter per tab, a new wait supersedes the old one, page changes release it).

Consent is unchanged: the native prompt ledger still decides, nothing installs without Add Extension, and Cancel, Escape and closing the store tab answer the prompt with a cancel.

## Tests

| Command | Result |
| --- | --- |
| `node … --test src/hostPageCommands.test.mjs` | new test: with a store wait pending, `tab.viewport` and `focus` still go out. Fails before the fix ("the viewport waited behind the store request"). |
| `pnpm --filter @ardia/linux test:host` | 533 passed |
| F09S (ee23b331 engine, network, this branch's dist) | 3 of 3 checks pass: the dialog lists the permission, Cancel installs nothing and the listing offers Add again; closing the store tab dismisses it; Add Extension installs from the store and Settings lists it. With a dist built from `linux`: 0 of 3. |

No native change, so no new engine build is needed.
