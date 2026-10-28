# F11 handoff: Live folder services on Linux (#21, source work #222)

Live folders on Linux now connect to GitHub, Bitbucket, Notion, Confluence and Google Drive and refresh through a native service in the browser process. The renderer never makes these requests and never holds a token after it hands one over. Built and tested from source; none of it has run in a Chromium 156 build yet (no engine build was allowed in this lane).

## Shape

| Piece | File | What it does |
| --- | --- | --- |
| Core (plain C++) | `engine/ardiacore-linux/src/ardia/linux/ac_linux_live.{h,cc}` | `LiveCore`: the fixed provider operations, typed credentials, the request queue, cancellation, credential cache and Google renewal. Talks to a `Transport` and a `CredentialStore` interface. |
| Chromium glue | `ac_linux_live_service.{h,cc}` | `SimpleURLLoader` transport, the keystore on a blocking worker sequence, caller checks, request-id namespacing. Routed from `ShellHostView::Invoke`, so only the trusted `chrome://ardia/` shell can call it. |
| Keystore | `ac_linux_sync_keystore.{h,cc}` | #162's Secret Service and file stores, now parameterised by a `KeySchema`. Sync keeps `dev.ardia.linux.SyncKey`; Live uses `dev.ardia.linux.LiveToken` with label "Ardia Live Folder Token" and the source name (`github`, …) as the account. |
| Renderer client | `apps/linux/src/hostLive.ts` | `ArdiaLive.*` calls, request ids, `AbortSignal` → `ArdiaLive.cancel`, native error code → `LiveError` kind. |
| Sources | `apps/linux/src/liveSources.ts` (replaces `state/live/sources.ts`) | Same exports and mapping as the shared module (it reuses the shared GitHub/document mappers), but every network step is a fixed native operation. |
| Endpoints | `apps/linux/src/liveNet.ts` (replaces `state/live/net.ts`) | Only what `googleAuth.ts` and the Settings pane still need: the production endpoint strings, `form`, and `isMocked = false`. The shared `request`, `fetch` and keychain helpers are gone from the Linux bundle. |
| Shell hook | `apps/linux/src/liveHooks.ts` | `ardiaLinux.live` for acceptance scenarios, like `ardiaLinux.sync`. |

The shell CSP is unchanged (`connect-src 'none'`).

## Native contract

`ArdiaLive.request(source, operation, params, id)`, `ArdiaLive.connect(source, fields, id)`, `ArdiaLive.cancel(id)`, `ArdiaLive.disconnect(source)`.

Operations the renderer may ask for:

| Source | Operation | Request built natively |
| --- | --- | --- |
| github | `graphql` {query, variables} | POST `https://api.github.com/graphql`, Bearer. The query must be read-only: any `mutation` or `subscription` token outside a string or comment is refused. Comments end at `\n`, `\r` or `\r\n` as in the GraphQL spec, and a `\r` or `\n` inside a single-line string is refused. |
| github | `deviceCode` {clientId} | POST `https://github.com/login/device/code`, scope `repo read:org`. No token. |
| bitbucket | `pullRequests` {user} | GET `https://api.bitbucket.org/2.0/pullrequests/<user>?state=OPEN&pagelen=50`, Basic. |
| bitbucket | `statuses`, `pullRequest` {repo, id} | GET `/2.0/repositories/<ws>/<slug>/pullrequests/<id>[/statuses?pagelen=50]`. `repo` is exactly two `[A-Za-z0-9._-{}]` segments; `id` a positive integer. |
| notion | `search`, `users` | POST `/v1/search` with the fixed body, GET `/v1/users?page_size=100`, Bearer, `Notion-Version: 2022-06-28`. |
| confluence | `search` | GET `<site>/wiki/rest/api/content/search?…&cql=<fixed CQL>`, Basic. `site` is the one stored with the token. |
| gdrive | `files` | GET `https://www.googleapis.com/drive/v3/files?<fixed query>`, Bearer access token (renewed natively). |

Connect verifies the credential with the provider before it stores anything (GitHub viewer, Bitbucket `/user`, Notion `/users/me`, Confluence `/user/current`, Google's code exchange then `/about`). The reply is the provider's verification JSON, never the token. The GitHub device flow polls through `connect("github", {clientId, deviceCode})`; its replies are `{status: "pending" | "slowDown" | "connected"}`, and on `connected` the token is verified and stored natively.

The native fixed strings (viewer query, Notion body and version, Confluence CQL, Drive query, Google redirect, GitHub scope) are compared with the shared macOS constants by `test_live.py`, so a change to the shared module fails the test until native follows.

### Limits

- HTTPS only, no redirects (`RedirectMode::kError`), no cookies (`CredentialsMode::kOmit`), no cache. The app profile's network context, so proxies apply.
- 20 s timeout per request, 8 MB per reply, replies must be UTF-8 JSON objects or arrays.
- 8 requests in flight across the app, 256 queued; more fail with `ERR_LIVE_BUSY`.
- 64 KB of arguments, 16 KB GraphQL query, 4 KB per credential field.
- Confluence sites must be `https://host[:port]` with a dotted host and no path, user info or query. macOS also accepted `http://`; Linux refuses it so a token never goes out unencrypted.
- Google's redirect must be `http://127.0.0.1:53682/ardia-oauth`, the shared sign-in page's loopback address.

### Errors

| Code | `LiveError` kind |
| --- | --- |
| `ERR_LIVE_NOT_CONNECTED` | `signedOut` if Live has the account, otherwise `notConfigured` (as macOS's missing keychain item) |
| `ERR_LIVE_SIGNED_OUT` (401, 403 without rate-limit headers, Google `invalid_grant`) | `signedOut` |
| `ERR_LIVE_SSO` (403 with `x-github-sso`) | `sso` |
| `ERR_LIVE_RATE_LIMITED` (429; 403 with `x-ratelimit-remaining: 0`, a `retry-after` header, GitHub's "secondary rate limit" message or a Google `usageLimits` error) | `rateLimited` |
| `ERR_LIVE_NETWORK` (no response, timeout, refused redirect) | `network` |
| `ERR_LIVE_KEYRING_LOCKED`, `ERR_LIVE_KEYRING_UNAVAILABLE`, `ERR_LIVE_CANCELLED`, `ERR_LIVE_ARGUMENT`, `ERR_LIVE_BUSY`, `ERR_LIVE_SERVICE`, `ERR_PRIVATE_SERVICE` | `other`, with the native message |

Two deliberate differences from macOS:

- A revoked Google refresh token (`invalid_grant`) reads as signed out ("Sign back in to Google Drive") instead of a generic 400.
- GitHub's secondary rate limit (403 with `retry-after`, or its "secondary rate limit" message) and Google Drive's documented usage-limit 403s (`userRateLimitExceeded`, `rateLimitExceeded`, `dailyLimitExceeded`) read as rate limited. The shared macOS mapping (`state/live/net.ts`) still reads them as signed out (#241's contract tests found it).

## Tokens

- Stored per source as a fixed-field record (`ardia-live-1`, then each field hex-encoded in a fixed order). A record with a missing, extra or reordered field is rejected. Nothing generic from the renderer is stored.
- Secret Service by default. A background instance with an explicit, non-default `--user-data-dir` uses `<user data>/Ardia Live Tokens/live-token-<source>` (0600), the same rule as sync; acceptance runs use it.
- Credentials are cached in the browser process after the first load, so refreshes don't hit D-Bus. Strings holding secrets are wiped when freed.
- Sources and service never log (checked by `test_live.py`).

## One authority

- Refresh: F04's document claims already make the first window to claim a folder its only refresher until that window unwatches. The F11 acceptance scenario checks that two windows refreshing at once fetch once.
- Credentials and renewal live in one process-wide `LiveCore`. Concurrent Drive refreshes with an expired token share one renewal. A Drive 401 on a token that hasn't expired yet forces one renewal and one retry.
- Connect and disconnect bump a per-source generation, so a load or renewal that finishes after a disconnect can't bring the token back. Disconnect cancels that source's in-flight requests.
- A connect bumps the generation when it posts its keyring save and again when the save commits. A renewal that spans either point, or finishes while a connect save is pending, is discarded and never written over the new token.
- A connect that is cancelled, or whose renderer gave up (a bridge timeout makes `hostLive` cancel it), restores the token that was stored before, or deletes the item if there was none. Keyring and UI agree that nothing changed. A newer connect or a disconnect wins over that restore.
- When a shell renderer retires (reload, crash, close) or its window goes away, `ShellHostView` calls `LinuxLiveCallerGone`, which cancels that caller's in-flight requests and frees their slots.
- Private windows (and an off-the-record app profile) get `ERR_PRIVATE_SERVICE`. Settings is a regular utility window and can connect.

## Tests

- `engine/ardiacore-linux/tests/test_live.py`
  - builds `tests/fixtures/live_host_tool.cc` with ASan/UBSan and runs its self-test (GraphQL read-only rules, site rules, credential tamper, schema names);
  - checks Secret Service without a bus reports `unavailable`;
  - runs `tests/live_services.test.mjs`: the real `liveSources.ts` and `hostLive.ts` against the native core over sockets to local fake provider servers (`apps/linux/scripts/acceptance/lib/liveServices.mjs`), with `fetch` replaced by a thrower. It covers connect, refresh, restart, revocation, renewal (single-flight, expiry, server-side revocation), rate limits, SSO, offline and recovery, cancellation, queue bounds, the device flow and its cancellation, refused operations, a locked keyring, disconnect during a request, and an unavailable host;
  - compares the native fixed requests with the shared constants;
  - source checks: no logging, GN registration, routing only through `ShellHostView::Invoke`, private-window refusal, `kOmit`/`kError`/HTTPS in the loader, bootstrap deadlines, no network or storage APIs in the Linux Live modules.
- The Chromium glue itself (`ac_linux_live_service.cc`) is compiled against stubs (`tests/fixtures/live_service_support.h`) and driven through `LinuxLiveInvoke` and `LinuxLiveCallerGone` (`live_service_scenarios.cc`). The fixture covers:
  - refusal of private, off-the-record, shutting-down and missing profiles;
  - renderer request-id rules and per-caller namespacing (another window's cancel or retirement doesn't touch a request);
  - the loader's flags, headers, upload, timeout, size cap and traffic tag;
  - UTF-8, size, rate-limit and network mapping;
  - the 0600 token file, and disconnect deleting it.
- `apps/linux/src/hostLive.test.mjs` (in `test:host`): error mapping, request ids, abort and late replies, unavailable hosts, reply parsing.
- `apps/linux/scripts/module-graph.test.mjs`: the shared `sources.ts` and `net.ts` are absent from the production graph, and every Live importer uses the Linux modules.
- Acceptance `F11` (`scenarios/f11-live-services.mjs`): needs an engine built with this change. The fixture server now answers the provider hosts over the trusted test certificate, mapped by `--host-resolver-rules`, and can go offline.

## Not done / needs a person

- Not run in Chromium 156: the glue passes `scripts/linux/syntax-check-156.sh`, but the acceptance scenario waits for an engine build.
- Real accounts and OAuth client IDs for all five providers (manual steps in the scenario).
- A real desktop keyring: the item in Seahorse/KWallet, and the locked-keyring prompt.
- The shared Google OAuth flow's weaknesses (Math.random state and verifier, plain PKCE, the code on a loopback URL that every window sees) are tracked in #233. They're shared with macOS and unchanged here.
- Google sign-in still opens the consent page in an Ardia tab and watches for the loopback redirect, as macOS does. From the Settings utility window that tab is created in the browser window, so the redirect has to come back through session sync. The acceptance scenario drives it from a browser window.

## Provider contract tests (#241)

`engine/ardiacore-linux/tests/live_provider_contract.test.mjs`, run by `test_live.py`, drives the real `liveSources.ts`, `hostLive.ts` and native `LiveCore` against replies shaped like each provider's documented API. The fixtures are in `tests/fixtures/live-contract/<provider>.json`, from these docs:

| Fixture | Documentation |
| --- | --- |
| `github.json` | GraphQL rate limits: https://docs.github.com/en/graphql/overview/rate-limits-and-query-limits-for-the-graphql-api · device flow: https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps#device-flow · SAML SSO and errors: https://docs.github.com/en/rest/using-the-rest-api/troubleshooting-the-rest-api |
| `bitbucket.json` | pull requests and statuses: https://developer.atlassian.com/cloud/bitbucket/rest/api-group-pullrequests/ · pagination: https://developer.atlassian.com/cloud/bitbucket/rest/intro/#pagination · API tokens: https://support.atlassian.com/bitbucket-cloud/docs/using-api-tokens/ · limits: https://support.atlassian.com/bitbucket-cloud/docs/api-request-limits/ |
| `notion.json` | search: https://developers.notion.com/reference/post-search · users: https://developers.notion.com/reference/get-users · errors: https://developers.notion.com/reference/status-codes · limits: https://developers.notion.com/reference/request-limits |
| `confluence.json` | CQL content search: https://developer.atlassian.com/cloud/confluence/rest/v1/api-group-content/#api-wiki-rest-api-content-search-get · current user: https://developer.atlassian.com/cloud/confluence/rest/v1/api-group-users/#api-wiki-rest-api-user-current-get · rate limiting: https://developer.atlassian.com/cloud/confluence/rate-limiting/ |
| `google.json` | OAuth for installed apps: https://developers.google.com/identity/protocols/oauth2/native-app · Drive errors: https://developers.google.com/workspace/drive/api/guides/handle-errors · files.list: https://developers.google.com/workspace/drive/api/reference/rest/v3/files/list |

What it pins, per provider:

- **GitHub:** the viewer and pull-request shapes; the documented 5 s device-flow interval; `authorization_pending`, `slow_down` (new interval 10), `expired_token`, `access_denied` and `device_flow_disabled`; the form body of each device-flow request; 401, the primary rate limit (200 with `RATE_LIMITED`), the secondary rate limit with and without `retry-after`, and SAML enforcement.
- **Bitbucket:** Basic auth with the Atlassian account email and an API token; the paginated list (only its first page of 50 is read, as on macOS); build statuses; a merged pull request; 401, 429, and a 403 for missing scopes (signed out).
- **Notion:** the bot user's workspace name; pages and databases, trashed ones skipped; `has_more` is not followed (30 newest, as on macOS); 401, 429, 403 `restricted_resource` (signed out) and 529.
- **Confluence:** the account id; a page with expanded space and history, its URL from `_links.base`; `_links.next` is not followed; 401, 429 and 403.
- **Google Drive:** the code exchange with `refresh_token_expires_in`; a renewal reply without `refresh_token` keeps the stored one; an `authError` on an unexpired token renews once and retries; usage-limit 403s and 429 are rate limits, a missing scope is signed out; `invalid_grant` on renewal is signed out.
