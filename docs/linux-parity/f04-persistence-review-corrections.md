# F04 post-review source corrections

The independent review requested changes on `8fa97a40..b0869aff`. All five findings were reproduced before editing with the complete probes under `/tmp/ardia-f04-independent-review/`. The earlier four implementation commits and their dependency commits remain intact. This correction range adds source repairs and regression fixtures; it does not establish native Chromium 156 acceptance or close #14, #21 or #24.

## Reproduced failures

| Finding | Before correction |
| --- | --- |
| Malformed stale refresh | Commit succeeded, retained a disallowed receipt field, failed canonical validation and caused the next edit to reject with `ERR_DOCUMENT_DATA`. |
| Deferred completion | A superseded refresh retained item `i` but its independent completion timer submitted an ordinary entry deletion and removed that item without a completed entry. |
| Non-owner claim | Nine claim attempts remained pending; a later Calendar edit retried nine more claims, sent zero Calendar operations and stayed dirty. |
| Same-epoch reconnect | The canonical order contained one folder while pending create projection displayed it twice and stayed dirty despite own watermark 1. |
| Generation ceiling | Rename at generation 2147483000 persisted 2147483001, failed envelope validation and made the next edit reject. |

The original malformed native probe intentionally dereferences the invalid receipt it created. After correction that receipt does not exist; `test_document_validation.py` verifies rejection and absence directly instead of relying on that unsafe diagnostic print.

## Native correction

Public `LinuxDocuments` method signatures and wire methods remain unchanged. Complete operation validation runs before every reducer stale/conflict branch, including all nested refresh operations, optional order collections, preconditions, metadata values and operation field allowlists. Receipt operations are validated by the canonical envelope validator as well. The finished envelope is validated with that same strict validator before persistence; malformed requests cannot create a receipt, canonical change, durable write or changed event. Errors contain fixed messages rather than supplied field values. Valid stale refreshes still commit a superseded no-op and allow the next operation to progress.

Every generation advance uses the checked helper with the same 2147483000 ceiling. This covers mutations, claims, owner retirement, authority recovery and volatile retirement floors. Exhaustion returns `ERR_DOCUMENT_LIMIT` before a new receipt/write/event, preserving the last valid durable envelope. The existing void `Unwatch` signature remains unchanged: it removes the subscription, detects exhaustion without writing invalid metadata, and marks the authority exhausted so later commits reject explicitly. Recovery at an exhausted owned generation also refuses without rewriting storage. No wraparound, implicit generation reset or tombstone compaction is introduced.

Unwatch now rereads current storage before retirement; it must not overwrite newer metadata from its cached envelope. A service-owned retired-client marker survives a failed read/write, alongside the retained generation floor, until a successful transaction durably fences the owner. Close seals still survive unwatch/rewatch and only matching native cancellation removes them. Lost-reply retry of the last accepted request remains idempotent even at the sequence ceiling.

The sole approved reply extension is optional `reason:'notOwner'` on `status:'conflict'`, emitted only for a fully validated authenticated singleton existing-incarnation claim held by another native client. Missing/stale/malformed claims and mixed batches do not receive that discriminator. Native host glue passes the helper result dictionary unchanged; no new domain method, operation type, event channel, GN source or storage callback is needed.

## Renderer correction

Only that exact singleton producer refusal becomes a terminal skipped result. The renderer verifies request ID, client, scope, epoch, revision base and an own watermark proving the sequence was not accepted. It then retires that unaccepted producer and reuses the unconsumed sequence for following work. Storage/network errors, unknown reasons, malformed/stale replies and mixed/user intent are retained. Accepted lost-reply claims retry their original identity and subsequent user edits advance to the next sequence normally. `beginRefresh` returns null for the terminal skipped producer; the engine stops that producer without changing the durable user queue.

Every authenticated watch resnapshot reconciles pending request sequences against its own watermark before projection, in both the same epoch and a replacement epoch. Client/scope identity and sequence ranges are validated before removing work. Covered accepted sequences are removed, unaccepted work remains, and drain waits for the current resnapshot before assigning its next operation base. Superseded connection replies cannot overwrite the new connection or reinterpret its request identity. Create projection also avoids duplicate order membership.

## Completion correction and shared seam

The shared store remains the only Live store. Default macOS/demo persistence still uses synchronous load, existing schema/defaults, the 800 ms save timer and the original completion animation timer/ordinary cleanup. Its new completion hook simply allows the existing behavior.

The shared engine passes the originating refresh fence through its completion timer. Shared state records completion origins per folder and item, and invalidation removes only items tagged with that exact origin. Overlapping timers and identical item IDs in another folder cannot clear each other's transient state. Linux tracks completion tickets separately from canonical/pending data; tickets bind the folder, incarnation, epoch, service owner and the acknowledged post-refresh generation.

Linux authorizes delayed cleanup only after an explicit committed, non-superseded refresh reply. A source event by itself, a lost/held reply, failed or superseded refresh, changed generation, deletion or recreation cannot authorize cleanup. Those paths invalidate only matching completion transient state. The conservative response to an unacknowledged timer is to skip its cleanup, retaining canonical data. Successful cleanup is encoded as the existing fenced `refresh` operation with the acknowledged generation, incarnation and owner; there is no new native wire operation. A generation change after cleanup submission is still checked natively before deletion.

Shared-only API additions are `LiveCompletionInvalidation`, a local `completion` intent, optional local `completionIds`, and `LivePersistenceBinding.completionFence`. `start` may pass matching completion invalidations to the shared domain. None of those local intent fields are blindly spread into native operations.

## Verification and remaining handoff

New native fixtures reject malformed stale/nested/mixed requests without writes/receipts/events, then accept a valid stale no-op and a good following edit. Ceiling fixtures cover rename, delete, claim, retirement, recovery, failed persistence/volatile floor and last-sequence retry. The prior seal/floor probes remain green.

New renderer fixtures cover definitive refusal followed by Calendar/close drain, unproven/spoofed/stale discriminator replies, generic storage failures, accepted lost replies, same/new-epoch resnapshot, unaccepted queue preservation and no duplicate create order. Real shared engine/store/Linux binding tests use deterministic timers for superseded/rejected/lost/held replies, actual successful completion, overlapping timers, same IDs across folders, configuration changes during fetch, deletion/recreation and generation changes after cleanup submission. A default binding test exercises its actual unchanged engine completion timer and persisted cleanup.

The native host/GN/profile authorization/service lifetime/reserved routes/close integration lane remains separate. Add the new `liveCompletionPersistence.test.mjs` to any explicit renderer test script; native unittest discovery includes `test_document_validation.py`. The existing Vite alias remains unchanged and its production graph assertion still applies. A fresh independent source review and joined matching Chromium 156 runtime scenarios are required before acceptance.

Final correction verification passed 125 joined Linux source tests, including the production alias graph, and all 25 native Linux fixtures. Both Linux and shared browser typechecks passed. The aliased Linux production build passed. The shared browser suite reported 913 passes and one skip; its sole failure remains the three pre-existing environment-read convention violations in `ac_input_frame_rate_linux.cc` and `pip/ac_pip_hyprland.cc`, with no added violations or baseline edits. The original reviewer seal/floor followup probe remains green; its same-epoch reconnect probe now reports one visible folder and dirty false, and its completion probe retains the canonical item and sends no ordinary entry deletion. Before/after evidence is source-fixture evidence only. A new independent review and matching native integration/runtime acceptance remain required.
