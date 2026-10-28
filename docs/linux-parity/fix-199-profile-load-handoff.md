# Fix handoff: a profile that can't load fails at once (#199)

Branch `t3/fix-window-profiles-199`, based on `linux` at `1d55d564`. Native (`ac_linux_profile_service.cc`).

## Root cause

The phantom window was not missing a refusal path. `createHostWindowOpener` already drops the window when `ArdiaShell.openWindow` fails. The reply just never came in time. With the user-data dir read-only, Chrome's `ProfileManager::CreateProfileAsync` for `Profile <id>` never ran its callback: Chrome's async profile creation ignores a failed `CreateDirectory` and goes on. So the registry load hung, and every caller waited for it:
- the window opening;
- the history, bookmarks and extensions reads for that profile.

Only the opening ledger's 30 s expiry ended it with `ERR_PROFILE_UNAVAILABLE`, after which the opener dropped the window. The acceptance check waits 20 s. The browser log of the run on the `e051e893` build shows the engine calls timing out at 30 s and "Chrome could not open this profile" right after.

## Fix

Before it asks Chrome to load an isolated profile that isn't loaded yet, the Chromium registry backend (`ChromeProfiles::Load`) checks on a blocking task that the profile directory exists or can be created, and that it is writable (`ProfileDirectoryWritable`). If not, the load ends with no profile. The registry then fails every waiter with `ERR_PROFILE_UNAVAILABLE` straight away, so `openWindow` rejects and the opener drops the window. A profile Chrome already has loaded skips the check. The 30 s expiry stays as the backstop for any other hang.

## Tests

| Test | Before | After |
| --- | --- | --- |
| `test_profile_host.ProfileServiceTests.test_a_profile_directory_that_cannot_be_written_fails_the_load_instead_of_hanging` | fail | pass |
| `python3 -m unittest test_profile_host` | | 11 pass |
| `scripts/linux/syntax-check-156.sh ac_linux_profile_service` | | ok |

## Needs a Chromium 156 build

This was not compiled into a build or run. After the next incremental build, run `--only F02`: "failed profile creation leaves no phantom window" should pass, with the window leaving the store within a second or two.
