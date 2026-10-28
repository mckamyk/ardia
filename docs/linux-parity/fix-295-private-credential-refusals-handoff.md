# #295: every credential call from a private window is refused as private

Issue: [#295](https://github.com/mckamyk/ardia/issues/295), found by the desktop-session harness (#240, F07P). Branch `fix2-private-refusals`.

## Cause

`ArdiaPasswords.list` and `ArdiaAutofill.addresses` aren't Linux host methods. The renderer lists passwords and addresses through the generic `passwords.list` and `autofill.addresses` engine calls, and those are refused as private. `ShellHostView::Invoke` only sends the nine typed credential methods (`credentials::kCapabilities`) to the credential service, so these two names fell through to "Unsupported Linux host method". `bootstrap.js` reports a failure without a code as `ERR_ENGINE`. That's why the regular window got `ERR_ENGINE` too: it wasn't the locked vault.

A second, smaller gap: for the typed methods, `Run` checked the arguments before the private-window check. So a malformed typed call from a private window answered `ERR_CREDENTIAL_REQUEST` or `ERR_PROFILE` instead of `ERR_PROFILE_PRIVATE`.

## Fix (Linux only)

- `credentials::ClaimsInvoke` claims every `ArdiaPasswords.*` and `ArdiaAutofill.*` method for the credential service, not only the nine it implements. Unknown names in those namespaces answer `ERR_UNKNOWN_CALL` in a regular window.
- `credentials::PlanWindowInvoke(private_window, …)` refuses a private window with `ERR_PROFILE_PRIVATE` and `kPrivateCredentialsMessage` before it parses or routes anything. The profile resolver's own private checks stay.
- Capabilities are unchanged: still the nine typed methods.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_credentials` | `credentials_scenarios`: every typed method, the two unknown names and malformed calls from a private window are `ERR_PROFILE_PRIVATE`; unknown names from a regular window are `ERR_UNKNOWN_CALL`; regular plans are unchanged. A source test checks that the service routes through `ClaimsInvoke` and `PlanWindowInvoke`. Both fail on `linux`. |
| `python3 -m unittest test_credentials test_private_services test_private_windows` | 15 pass |
| `bash scripts/linux/syntax-check-156.sh ac_linux_credentials.cc ac_linux_credential_service.cc` | 2 of 2 |

## Needs the next build to confirm

F07P's "the private window's renderer gets ERR_PROFILE_PRIVATE for passwords and autofill calls…" should pass. In the regular window, the two typed names now answer `ERR_UNKNOWN_CALL`, which isn't one of F07P's control calls.
