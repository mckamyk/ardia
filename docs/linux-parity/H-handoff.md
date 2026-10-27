# Lane H handoff: automated native acceptance suite (#81)

Branch `t3/linux-acceptance-suite`, based on `linux` at `bb9c745e`. This lane adds new files under `apps/linux/scripts/acceptance/` and `docs/linux-parity/`, plus two script entries in `apps/linux/package.json`. No product, shared, native or macOS code changed.

## What was done

`pnpm --filter @ardia/linux acceptance -- <executable> [uiDir] [--only …]` runs one scenario module per feature issue against a packaged Linux build. It runs headless with throwaway profiles and prints the result for each issue (`pass`, `fail` or `manual`) as JSON. Usage, environment, the automated checks for each issue and the manual list are in [acceptance-suite.md](acceptance-suite.md).

- `run.mjs` is the CLI. `lib/` holds:
  - the CDP client;
  - the launcher, which gives each browser its own process group, a scratch `HOME`, no session bus, no CUPS, and cleans up on Ctrl+C;
  - the shell and page driver;
  - the fixture servers and the TLS generation;
  - session and preference seeds;
  - the scenario runner (deadlines, critical checks, cleanup, evidence);
  - the report;
  - nested-compositor detection;
  - pure classifiers.
- `scenarios/` holds F01, F02, F03, F23, F08, F20, F10, F22, F26 and F27.
- `tests/` holds `acceptance:self-test`.

## Contracts the scenarios depend on

These come from the handoffs of #61–#65 and #68, and from the current shared UI:

- **Shell globals:** `ardiaLinux.store` / `ardiaLinux.emitCommand`; `__ardiaHost.bootstrap.{windowId,startup,utility,pageProfile,privateWindow}`; `__ardiaHost.invoke` and `__ardiaHost.call(name, profile, argsJson)`.
- **Native calls and errors:**
  - `ArdiaShell.windowIds`;
  - `tab.create` refusing with `ERR_PROFILE_*`;
  - `downloads.{pause,resume,cancel,list,setSettings}`;
  - `ArdiaSystem.fileStatus`;
  - `sites.zoomLevels`.
- **Shared store:** `s.find[tabId]`, `s.tabs[id].zoom`, `s.live[id]`, `s.downloads`, `s.windowUi[w].downloadsOpen` and `s.history[profile]`.
- **Visible labels:** `Site Controls`, `Go Back`/`Go Forward`, `Zoom: N%`, `Find on Page`, `Reset`, `Skip`, the onboarding step titles and the Settings pane names.
- **Launch behaviour:**
  - Background instances refuse print and DevTools, so F01's native-UI run launches without `ARDIA_BACKGROUND` but stays on ozone headless.
  - `--ardia-downloads-dir` and `--ardia-ui-dir`.
  - Documents and `linux-session.json` live in `<user-data>/Default/Ardia/`.

## Tests run

| Command | Result |
| --- | --- |
| `pnpm --filter @ardia/linux acceptance:self-test` | 46 passed, 0 failed. This includes the 8 checks run against stock `/usr/bin/chromium` 155. |
| `node scripts/acceptance/run.mjs /usr/bin/chromium --only F03` | Fails fast at "browser shell renderer" after 25 s, as it should for a non-Ardia binary. The failing scratch profile is kept and no test process remains. |
| `node scripts/acceptance/run.mjs --list` / bad arguments | Lists the 10 scenarios; exits 2 with usage on bad arguments. |
| `pnpm --filter @ardia/linux typecheck` | Passed |
| `pnpm --filter @ardia/linux test:host` | 202 passed |
| `pnpm --filter @ardia/linux build` | Passed |
| `node --test engine/ardiacore-linux/tests/bootstrap.test.mjs` | 7 passed |
| `python3 -m unittest discover -s engine/ardiacore-linux/tests` | See the PR body. This lane changes no native file. |

These were found and fixed while validating against stock Chromium:

- `--disable-features=MixedContentAutoupgrade` has no effect.
- Loopback image URLs never count as mixed content.

The mixed fixture therefore carries an insecure form action, which Ardia classifies as `mixed`.

## Still needs native runtime acceptance on a Chromium 156 build

I could not run any scenario against a real Ardia build. No Chromium 156 Linux build exists yet, and this lane must not build one. Every scenario is written against the joined contracts of the open PRs. None of it has run end to end. In particular:

- **Selectors and flows.**
  - The shell label and DOM selectors were checked against the shared source, not against a rendered shell.
  - The history-menu right-click, the toolbar reveal by hover and the Settings pane clicks are the most likely to need adjusting on the first run.
- **Headless-only mechanisms on Ardia 156.** These were verified only on stock Chromium 155:
  - kiosk printing to Save as PDF;
  - SPKI trust for the browser-process suggestion loader;
  - CDP drag delivery into the shell.
- **Save Page.** It always needs a native file dialog, so only "accepted without a crash" is automated.
- **No compositor here.** No nested compositor is installed, so F22 has only run its manual fallback. The sway and weston path is unexercised.
- **Issue status.** The issues stay open until a real run reports them `pass`. A `manual` result still needs the listed steps.
