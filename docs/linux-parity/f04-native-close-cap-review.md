# F04 renderer-first close cap correction

This standalone native correction follows source `2dbf9b63e8e771d074556e9f31455a2a5c6da0bf` and docs head `d5e96d7dea857ff5439b0de2237e0f9fa6d6d543`. It preserves the independently reviewed shared preparation `4a53080e` unchanged. It does not change canonical helper/service policy, JavaScript, hook-owned source, Chromium patches, GN, manifests, package scripts, F01, F05 or PageState.

## Before evidence

The full `/tmp/ardia-native-2db-post-review.md` was read before editing. The supplied original probe ran unchanged against its frozen 61-input source extraction:

`PYTHONDONTWRITEBYTECODE=1 python3 /tmp/ardia-independent-2db-review/probe.py host-cap-handoff host-cap`

Log `/tmp/ardia-native-cap-before.log` reproduced renderer-first exhaustion after 1024 confirmed cancellations: `phase=4 writes_blocked=1 native_token=0 chrome_token_empty=1 destroyed=0`; reject returned `ERR_WINDOW_CLOSE_IN_PROGRESS`, deadline retained the gate and a later commit returned `ERR_DOCUMENT_CLOSING`. The Chrome-first control already passed and retained replay rejection.

## Authority before handoff

The posted CloseNativeWindow task still requires its exact weak host/browser/window, current committed renderer/close identity, regular authorization, successful Seal and clean window ledger. It now also requires the precise sealed/pre-handoff phase and no existing native or registry attempt. An existing progressing Chrome attempt causes the old task to return without rebinding or cancellation.

After those checks, the task mints and binds a new native token before HandOff and before BrowserWindow Close. Bind failure proves this exact task has never invoked Chrome and there is no existing bound attempt. Its existing local prebegin cancellation therefore awaits FileRunner CancelSeal and emits the exact cancellation event before producer resumption. The global handed-off-without-token refusal remains intact. A registry attempt not owned by the host also prevents independent local unsealing until its actual cancellation proof.

Successful binding is stored before HandOff. Any subsequent cancellation must pass the exact registry abort proof; a failed HandOff after binding uses that bound cancellation route. The task never resets a registration, evicts token history, reuses a retired token or bypasses the cap. Already irreversible original leases are unchanged.

## After evidence and fixtures

`/tmp/ardia-native-cap-after.py` uses the supplied scenario with the current owned source and permits the now-successful idempotent rejection response. Log `/tmp/ardia-native-cap-after.log` reports `phase=8 writes_blocked=0 native_token=0 chrome_token_empty=1 destroyed=0`, `cancel_ok=1 code=confirmed`, `after_deadline_blocked=0` and `later_commit_blocked=0`. The unchanged Chrome-first control still passes.

The 12 cap/ordering groups passed under ASan/UBSan (`/tmp/ardia-native-cap-composition.log`); the 14 existing extracted-host groups also passed (`/tmp/ardia-native-cap-host.log`).

`test_document_close_cap.py` compiles actual host/service, registry and selected patched controller/RFH methods under ASan/UBSan. Its cases exercise renderer-first and Chrome-first caps, repeated/lost acknowledgment replies, exact event identity after worker completion, idempotent reject/deadline, reentrant close, accepted failure and exact recovery, the last allowed binding's retained post-begin lease, cancellation/renderer replacement before the task, an already progressing Chrome race, an existing registry attempt and global unbound-handoff protection. A worker fault-injection case verifies that failed matching CancelSeal never emits producer resumption; only a subsequent successful matching cancellation does. The shared preparation, operation tickets, ledger bounds and watch identities are unchanged.

The composition uses simulated browser/base/storage endpoints and extracted production methods; it is not full Chromium156 runtime qualification. Existing committed-document authorization and production JS close-barrier fixtures remain required alongside it.

## Separate kernel dependencies

At this cap checkpoint the current three-argument admission API is preserved. The parent separately delivered force correction `26c602e3a913d0fd3471077504190112d3ad7196` and requesting-stage correction `1ad74032a5019a417d31c140b77d840220f731d9`, with scoped independent source acceptance. They are intentionally not part of this cap commit. Each must join as a separate dependency, followed by a separate host stage-plumbing commit and fresh combined review.

The requesting-stage contract is `CommitLinuxCloseTeardown(browser, token, stage, native_commit)` with no default or legacy overload. Both native sync/async paths must pass their exact requesting stage unchanged; the host must not guess a pending stage or set both retry flags. The kernel owns exact pending-stage lookup and the separate forced-retry correction.

Full Chromium156 compilation/link/runtime, full-series offset qualification, physical unload/storage/compositor/modality and exceptional destruction/crash/power-loss guarantees remain open. No engine/cache/profile/app/publication action is performed. Issues #14/#21/#24 remain open; F04 is unpublished.
