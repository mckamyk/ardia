# Linux domain, identity and bootstrap contract handoff

This implementation addresses the source contracts in #39 N02, #40 N03 and #41 N04. It does not establish native Chromium 156 runtime acceptance or full feature parity. Keep those issues open until matching-engine acceptance and independent review finish.

The starting branch contained Linux base `b70711df2af16cee08146b78f7d3405c57d6d1de` and planning top `d4df45f7`. No macOS, shared browser, demo, owner profile or Chromium build-cache file was edited.

## Dependency commits

The implementation branch separately cherry-picked the coordinator's dependencies for joined tests:

- N05 `dc9247a2a34b5533f499185cb9b71a6f372f9ba5` as `22f04171`.
- N01 `1614a764fbbbc2757c960f74373a47a8f875d15e` as `93516798`.
- N05 review correction `ab71e1f4fdde437a237864aba8e7e0ccb25edd6f` as `cf56dfb7`.

Integrate the owned implementation commit without duplicating those dependencies.

## Contract changes

Linux domain modules route current `call(verb, profile, JSONargs)` through a native allowlist. History, bookmarks, favicons, download commands/settings, extension services, site settings, search engines and supported engine services call existing `ac_*` functions. Local tabs/strips, windows, engine information, search selection and auto-PiP use the Linux host. Raw `ac_*` names and unavailable current calls reject. Secret-writing password/card calls are absent from the generic table; dedicated methods remain unavailable until implemented.

Native replies reject top-level engine errors and empty/malformed JSON. Bootstrap rejection preserves `code` and `message`; scalar, string, null and object success replies remain JSON text until the current shared wrapper parses them. Components/tasks/site/search result shaping follows the Swift domain mappings. Task tab IDs are translated through the requesting window's binding registry. Extension list manifest enrichment for popup/action/side-panel metadata remains part of F07 rather than being inferred from the raw engine list.

Current per-domain event envelopes contain `{name,data}`. The host checks native profile ownership, rejects private payloads, flattens download events and resolves tab identities in the receiving window. Strip notifications use the current tabs channel, including close transactions. Listener removal is idempotent and supports channel reuse.

Web views bind with the application's `props.tabId`, call `onReady()` with zero arguments and use that stable ID for overlay ownership. Views without an application ID receive a private temporary lifetime. Every view mount carries a unique claim; native commands, viewports, presentation batches and releases resolve that claim against the receiving window's native registry. Reclaim requires the current predecessor token, cross-window moves require an explicit native transfer offer, and adoption requires an offer recorded from an actual native open callback. Logical close works after view detachment. Temporary surfaces close on release. Renderer teardown clears its claims and an old handler cannot detach a replacement handler's viewports.

The host initializes the shared Linux launch configuration before its startup reads and test gate, then injects the complete native snapshot using actual document/user-data roots. `ArdiaApp.launchConfig()` is synchronous before shared domain import. Tests/traces stay disabled, new-tab prewarm stays off, and no environment dump reaches the renderer. The existing synchronous system-info adapter remains local. Other unimplemented native APIs, including engine input-idle reporting, still need separate capability treatment; they are not newly advertised as supported.

## Verification

Dependencies were installed locally with pnpm 11.1.3 under the repository pod lock using the coordinator's Linux `flock` adapter and the frozen lockfile. No manifest or lockfile changed for installation.

Executed from the repository root:

- `npm exec --yes --package=pnpm@11.1.3 -- pnpm --filter @ardia/linux typecheck` — passed.
- `npm exec --yes --package=pnpm@11.1.3 -- pnpm --filter @ardia/linux build` — passed, 1161 modules.
- `npm exec --yes --package=pnpm@11.1.3 -- pnpm --filter @ardia/linux test:host` — 76 passed using the current shared domain wrappers and Expo-only native fakes.
- `node --experimental-strip-types --test apps/linux/src/hostContract.test.mjs engine/ardiacore-linux/tests/bootstrap.test.mjs` — 14 passed, including an isolated subprocess loading the real Linux Expo adapter and current shared `engine/call.ts`.
- `node --experimental-strip-types --test apps/linux/scripts/module-graph.test.mjs` — passed.
- `python3 -m unittest discover -s engine/ardiacore-linux/tests -v` — 23 passed after the N05 correction. These compile extracted native methods and Linux-owned policy/launch/contract helpers; they do not compile the complete Chromium host.
- `git diff --check` — passed.

The original host suite could not load obsolete shared imports before N01; the original native fixture suite passed 11 tests before changes. A presentation fixture then caught its missing claim model during migration; its fixture now exercises the actual central claim guard and verifies that an invalid batch cannot mutate another tab. The production graph fixture initially exposed complete tree-shaking of the adapter after the obsolete provider was removed; the current typed launch-tab provider retains the adapter and its domain identity.

The coordinator should add `src/hostContract.test.mjs` to the UI-owned `test:host` manifest or invoke the explicit command above in CI. The existing package script was left to that owner.

## Remaining acceptance

No matching Chromium 156 source build/binary is available in this environment. The full modified `ac_linux_host.cc` has not been compiled, linked or executed against 156. No older 155 executable was used as evidence.

Independent review and a matching native build must still exercise current history/bookmarks/download settings/extensions/auto-PiP service calls, unknown/private-profile rejection, ordinary-page bridge isolation, fresh/restored production startup, live event/profile/window filtering, create/remount/adopt/reclaim, unauthorized transfer/takeover, late completion, renderer replacement and temporary extension surface disposal. Native permissions/media integration remains unavailable; generic JSON does not pretend those services exist. Physical focus, keyboard, cross-window transfer and compositor behavior remain desktop acceptance work.

## Independent review correction

The review of `c0610dc7` requested one P1 and four P2 corrections. All were reproduced before source changes. The failing logs are `/tmp/ardia-bridge-replacement-before.log`, `/tmp/ardia-bridge-renderer-before.log`, `/tmp/ardia-bridge-extension-before.log` and `/tmp/ardia-bridge-launch-before.log`.

Native handler registration now acquires a renderer generation once. Replacement retires predecessor claims before publishing the new handler, and request, reply, event and binding authorization check the current native generation. Late destruction of the predecessor cannot retire the replacement. Retirement detaches old viewports, closes temporary native contents through the normal native close path and retains ordinary logical tabs. Extracted native methods exercise both replacement/destruction orders and current-generation authorization.

`HostTab` now serializes creation, commands and release for each stable key. It publishes a replacement owner after successful native creation; a failed replacement retains its predecessor. Disposal waits behind pending replacement work, so failure releases the original claim and success cannot release the new one. Pending viewports/presentations wait for replacement decisions. Six real `HostTab` tests use a native-policy transport to cover failure, disposal, retry, concurrent replacement, pending commands and late success. Callback exceptions cannot undo an already accepted native claim.

`extensions.configure` validates and flattens the supported Swift options (`pinned`, `fileAccess`, `incognito`, `siteAccess`) before native dispatch. Unknown options, wrong types, unknown site-access values and empty option objects reject before mutation. The regression fixture captures JSON from the real shared wrapper and production Linux Expo adapter, reconstructs it as native values, and compiles/runs the actual Linux transform together with the existing `ac_extensions_configure` backend. The fixture verifies the requested changes reach that backend, rather than treating its prior success response as evidence of mutation.

Launch claims consume one native decision, including an empty claim. Nonempty claims require a matching native key, URL, profile and existing tab offer. Adoption reuses the exact owned contents and never calls general tab creation. There is currently no native early-launch offer producer on Linux; arbitrary nonempty launch claims therefore fail closed. Fixtures exercise repeat, mismatch, empty, absent/stale offers and the actual native offered-tab adoption method.

Correction verification, from repository root:

- `node --experimental-strip-types --test apps/linux/src/hostTabReplacement.test.mjs apps/linux/src/hostContract.test.mjs engine/ardiacore-linux/tests/bootstrap.test.mjs` — 20 passed.
- `npm exec --yes --package=pnpm@11.1.3 -- pnpm --filter @ardia/linux test:host` — 76 passed.
- `python3 -m unittest discover -s engine/ardiacore-linux/tests -v` — 29 passed.
- `npm exec --yes --package=pnpm@11.1.3 -- pnpm --filter @ardia/linux typecheck` — passed.
- `npm exec --yes --package=pnpm@11.1.3 -- pnpm --filter @ardia/linux build` — passed, 1161 modules.
- `git diff --check` — passed.

The parent integrator must include `src/hostTabReplacement.test.mjs` alongside the contract test in its default host-test script. Package/Vite files were not changed in this correction. F04 documents and close-barrier integration are deliberately outside this commit. Matching Chromium 156 compilation/runtime, real temporary-page teardown and compositor/renderer replacement acceptance remain unverified; these extracted fixtures do not establish those results.

## Second independent review correction

The review of `51551e93` found three further P2 races. Before editing, both independent probes reproduced them: `/tmp/ardia-bridge-round2-presentation-before.log` and `/tmp/ardia-bridge-round2-native-before.log`. The owned presentation tests also failed the exact one-microtask replacement and held-reply multikey sequences before repair.

Renderer activation now occurs only in the native `PrimaryPageChanged` callback. `RegisterMessages` registers transport without acquiring authority. `SetHandler(handler, ui)` validates the actual live primary frame and WebUI, stores a native `WeakDocumentPtr`, and retires the previous document when a real replacement commits. Native document validity rejects old authority even when a RenderFrameHost is reused. Canceled speculative registration and its destructor leave the still-committed renderer authorized. Temporary JavaScript disallow does not retire a still-owned committed document; actual renderer-process loss performs cleanup. Extracted production registration, activation, trust, generation and destructor methods exercise cancellation, real commit, wrong hosts, subframes, reused frames/documents and crashes.

Private temporary surfaces now use `DisposeTemporary`. It detaches the owned WebContents with the native deleted reason into a local `unique_ptr` and destroys it, so a deferred or canceled ordinary beforeunload cannot orphan the surface. Native ownership remains local across reentrant TabStrip, browser or host destruction. The ordinary `CloseTab` user-close path is unchanged. The compiled disposal fixture uses ASan/UBSan and exercises deferred/canceled unload, nested disposal, owner destruction during detach and transaction callbacks, late callbacks, and ordinary-page retention.

Presentation batching now reserves each affected stable-key queue synchronously when requested. Ownership changes on those keys wait until native batch submission/reply completes. Independently ready keys flush without waiting for an unrelated stalled replacement. Queue reservations retain preceding work even when a failed mount's presentation rejects. The existing pending-creation presentation test now verifies both valid ordered presentations: the predecessor update occurs before the replacement claims the key, followed by the replacement update. It no longer expects the earlier requested update to disappear.

Verification from repository root:

- `node --experimental-strip-types --test apps/linux/src/hostTabPresentation.test.mjs apps/linux/src/hostTabReplacement.test.mjs apps/linux/src/hostContract.test.mjs engine/ardiacore-linux/tests/bootstrap.test.mjs` — 25 passed, retaining all original review regressions.
- `npm exec --yes --package=pnpm@11.1.3 -- pnpm --filter @ardia/linux test:host` — 76 passed.
- `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/ardiacore-linux/tests -v` — 34 passed, including the sanitizer disposal fixture.
- Linux `typecheck` and `build` through pnpm 11.1.3 — passed; build transformed 1163 modules.
- Staged diff checks — passed.

F04 integration conflicts are limited to these Linux host seams: `ShellHostView::SetHandler(handler, content::WebUI*)`, `OwnsHandler`, `RetireRenderer`, `Trusted`, the new `DisposeTemporary`, and the temporary branch of `Invoke(tab.release)`; `HostMessageHandler` gains WebContents observation, committed activation, process-loss handling and revised JavaScript-disallow cleanup. Its constructor and `RegisterMessages` also change. Document-client activation must follow committed native activation rather than speculative registration. No F04 helper, registry, dispatcher, barrier, GN, Vite or package file is changed here. The parent should add `hostTabPresentation.test.mjs` to its default test script.

The public Chromium API references were read only from the existing 155 source headers; they are not 156 compile evidence. Complete Chromium 156 host compilation and actual navigation cancellation, unload and compositor acceptance remain open. The fixture results do not establish full native runtime behavior.
