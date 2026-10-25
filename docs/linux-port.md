# Ardia on Linux

The Linux port targets Arch Linux, Omarchy, Hyprland and Wayland. It reuses the browser UI through `apps/demo` and Chromium services through `engine/chromium`, with a separate Aura host in `engine/ardiacore-linux`.

The implementation is under active verification. Passing web builds and patch-context checks do not establish a working Linux browser. The full Chromium executable must still be compiled and exercised on Linux before this port can be called supported.

## Upstream merge objective (`Closes #1`)

The upstream owner, `@barre_of_lube` (`mantrakp04`), requested a working Linux build and CI before merging the port into [`mantrakp04/ardia`](https://github.com/mantrakp04/ardia). The upstream PR must include `Closes #1` for [Linux Support](https://github.com/mantrakp04/ardia/issues/1), pass the linked-issue check, and have the CLA signed on its PR thread when prompted by `.github/workflows/cla.yml`.

Keep macOS behavior and its CI intact. Linux additions belong behind `is_linux` in GN, with the Aura host in `engine/ardiacore-linux`. The live engine bridge activates when `window.__ardiaHost` is present; `apps/demo` must also keep working as the ardia.dev web demo.

Source, scripts and configs must contain no comments except shebangs, license headers, compiler/linter directives and a script's printed `--help` header. Put explanations here or in commit messages. Existing checks, including `pnpm typecheck`, `pnpm test` and platform-appropriate patch-series checks, remain upstream merge requirements; the verification guidance below does not establish that they have passed for a particular revision.

## Current implementation

- The build tools select `AC_TARGET_OS=mac|linux` and `AC_TARGET_CPU=arm64|x64`. Linux builds `chrome` in `Release_GN_linux_<cpu>`; the existing Mac framework output is unchanged.
- `helium-pins.ini` pins Chromium 155.0.8059.84, shared Helium 0.19.4, and Helium Linux 0.19.4.1 at `cf878d4aaff58d063beada276c437ae756a142aa`. That Linux revision references the same shared Helium commit as the Mac build.
- Every patch-series command understands platform selection. Shared lifetime changes remain on both platforms; Cocoa, Apple Safe Storage, Metal and other Mac changes stay on Mac.
- The Linux layer copies its own sources plus the shared hook declarations. It does not apply the Mac ArdiaCore startup or window-host scripts.
- Engine services have platform-selected implementations. Linux has protocol allowance operations, Aura input tracking and PiP placement/fade adapters. Linux OS reauthentication and the token-authenticated CLI explicitly report unavailable.
- The additive `ac_engine_call(name, profile_dir, args_json, reply, context)` dispatcher routes only generated known service names. Existing individually exported calls and ABI version 2 remain compatible.
- Web host mode uses a privileged browser UI, native tab calls/events, persisted documents and sessions. Plain browser demo mode keeps its seeded tabs and iframe implementation.
- The native host attaches Ardia UI and real-tab WebViews to Chromium's existing BrowserView. Chromium retains BrowserWindow and TabStripModel ownership and its normal browser delegates.

The first native host supports the default regular profile. Multiple profiles, private windows and further platform integrations must be enabled only after their implementations and isolation checks pass.

Some native host methods remain explicitly unavailable, including tab-strip grouping, capture commands, native confirm/prompt panels and profile-data deletion. These calls reject with an unsupported error. A successful web build does not mean every existing Mac feature has a Linux implementation.

## Architecture and boundaries

`ChromeBrowserMainParts::PostBrowserStart()` starts `ardia::StartLinuxHost()`. The Linux host serves the bundled UI at `chrome://ardia/`. Assets are loaded from `ardia-ui` beside the Chromium executable, or an explicit `--ardia-ui-dir` for development.

The UI receives `window.__ardiaHost` before its Vite modules run. Calls use named service methods and profile identifiers. The native bridge resolves identifiers to loaded Profile objects and executes service calls on Chromium's UI thread. Ordinary tabs must never obtain the shell's privileged bridge, including by navigating to the same internal URL.

The C service ABI is only part of the integration. Window and profile ownership, tab creation and adoption, view placement, permissions, browser dialogs and lifecycle events also need a native host. They are not provided by two JSON functions in `public/ac_engine.h`.

Each real tab stays in Chromium's TabStripModel. Native tab release detaches a viewport; an explicit close operation ends a tab. Moving or remounting a UI view must not close its tab. Rectangles arrive in CSS viewport coordinates and the host converts them to native coordinates. Native content must hide beneath browser overlays and must handle its own clipping.

Session and document writes run in order on a file worker. The final session snapshot is sent before the JS transport disposes. Host session storage is separate from upstream's versioned shell documents. Private and transient state is excluded from persisted host snapshots.

Session writes carry a baseline and a proposed snapshot. The native worker merges local changes into the latest stored snapshot, preserving changes made in other windows. Identity-based list merging retains concurrent additions. Canonical session events synchronize renderers while preserving pending local edits, live navigation and native window identifiers. Opening a new window does not reapply the app-launch restore policy.

The shell keeps Chromium's Trusted Types enforcement. Its private `ardia-symbols` policy accepts generated icon markup with escaped attributes; the CSP does not permit a default policy or string evaluation. Browser fixtures exercise this same policy rather than relying only on an unrestricted development page.

## Building on a Linux machine

Use a dedicated Linux build root. Never reuse or clean the owner's Mac Chromium build cache. The initial source preparation is for a fresh tree; `01-sync.sh` refuses to run when an output directory already exists. Subsequent builds are incremental.

Chromium's Linux prerequisites, a compatible Node/pnpm installation, Python, Git, util-linux `flock`, and ImageMagick or Pillow must be provisioned first. Helium's Docker build environment is a useful reference for dependencies. This fork's source-preparation flow has not yet been validated end to end on a Linux builder.

```bash
export AC_CHROMIUM_BUILD="$HOME/ardia-linux-build"
export AC_TARGET_OS=linux
export AC_TARGET_CPU=x64
mkdir -p "$AC_CHROMIUM_BUILD"

git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$AC_CHROMIUM_BUILD/depot_tools"
git clone --depth 1 --branch 0.19.4 https://github.com/imputnet/helium-chromium.git "$AC_CHROMIUM_BUILD/helium"
git clone --depth 1 --branch 0.19.4.1 https://github.com/imputnet/helium-linux.git "$AC_CHROMIUM_BUILD/helium-linux"

source engine/patches/build/env.sh
flock -x -w 30 /tmp/ac-chromium.lockf engine/patches/build/01-sync.sh --apply
flock -x -w 30 /tmp/ac-chromium.lockf python3 engine/patches/build/03-helium.py --platform linux --apply
scripts/linux/build.sh --apply --generate
```

Use `AC_TARGET_CPU=arm64` for ARM Linux. Source hooks request PGO profiles and GN uses `chrome_pgo_phase=2`; missing or incompatible profiles must be fixed before an official build can pass.

Inspect configuration without touching Chromium:

```bash
scripts/linux/build.sh --print-config
AC_TARGET_CPU=x64 scripts/linux/build.sh --print-config
python3 engine/patches/series.py files --platform linux
```

For an existing prepared Linux tree:

```bash
scripts/linux/build.sh --apply
```

Add `--generate` when GN arguments change. Do not rerun source synchronization or clean the output directory for an ordinary incremental rebuild.

Build and package the UI and runtime:

```bash
pnpm --filter @ardia/demo build
python3 scripts/linux/package.py \
  --out "$CB/chromium_git/chromium/src/out/$GN_OUT_CONFIGS" \
  --ui apps/demo/dist \
  --destination "$CB/artifacts/ardia-linux-$AC_TARGET_CPU.tar.xz"
```

The archive contains `bin/ardia`, `lib/ardia/chrome`, Chromium runtime resources, the `ardia-ui` bundle and desktop integration files. Packaging fixtures validate layout; a package produced from real Linux outputs still needs an installation and launch test. Run tests with a throwaway `--user-data-dir`, never an existing browser profile. Keep Chromium's sandbox enabled.

## Patch maintenance

Manifest entries are `chromium <patch> [mac|linux|all]` or `layer <directory> [mac|linux|all]`. Omitted selectors mean shared. Apply, capture-base, materialize, files and check all use the same selection.

Mac arm64 retains the existing `series-base`. Other targets use `series-base-<platform>-<cpu>`, and recorded platform metadata prevents reuse across incompatible targets. The Linux layer's shared headers are included in capture, materialize and drift checks even though their source lives in the Mac layer directory.

Do not classify a patch solely by its filename. For example, `chromium-window-hosted.patch` retains shared last-tab/window lifetime changes, while its Cocoa view behavior lives in `chromium-window-hosted-mac.patch`. CEF carryover and Helium branding also have shared and platform-specific sections.

The Web Store patch already derives OS and CPU from Chromium's build. Do not replace a hardcoded `os=mac` string; none is used for its platform query.

## Upstream synchronization

`check-upstream.sh` and `sync-upstream.sh` wrap `scripts/linux/upstream.py`; both accept `--help`, `--ref`, `--no-fetch` and `--json`. By default they fetch `upstream/main`, adding the `upstream` remote if absent and refusing to fetch an existing remote that points outside `mantrakp04/ardia`. Use `--no-fetch` with an existing local ref for a read-only report without changing remotes or fetching.

The report compares the merge base with the selected upstream ref. `ENGINE_FULL` means actual Chromium version or selected Chromium/Helium pin identities changed; editing those files without changing the identities does not itself require a full rebuild. Ordinary engine, Linux host, patch or Linux tooling edits produce `ENGINE_INCREMENTAL`. UI and workspace dependency changes produce `UI_ONLY`; unrelated changes produce `NONE`. JSON and human output include `engine_tree`, the first 12 characters of `git rev-parse HEAD:engine`, even when upstream is already incorporated. This identifies the current committed engine tree, not the incoming tree or uncommitted edits.

Preview upstream changes, then explicitly apply the rebase:

```bash
scripts/linux/check-upstream.sh
scripts/linux/sync-upstream.sh
scripts/linux/check-upstream.sh --no-fetch --ref origin/main --json
scripts/linux/sync-upstream.sh --apply
```

Sync defaults to a preview. Only `--apply` rebases the current branch onto the resolved ref; it requires a clean working tree and an attached branch. Dependencies, UI checks, engine builds and pushes are separate commands. Sync does not guarantee a conflict-free rebase.

After resolving any rebase conflicts, install dependencies under the shared lock, run the relevant checks and build the UI separately:

```bash
scripts/agent/locked pod -- pnpm install --frozen-lockfile
pnpm typecheck
pnpm --filter @ardia/demo build
```

If the report calls for engine work, follow the dedicated Linux build and packaging workflow above, preserving existing output directories. Inspect the resulting changes and choose the intended destination branch before pushing separately, for example:

```bash
git push --force-with-lease origin HEAD:t3/linux-build-patches
```

## Planned prebuilt-engine CI (not implemented)

Linux CI must follow the prebuilt-engine model in [`.github/workflows/build.yml`](../.github/workflows/build.yml) and [`packages/ardiacore/scripts/fetch-engine.sh`](../packages/ardiacore/scripts/fetch-engine.sh). GitHub Actions should consume an engine built on the dedicated Linux server rather than compile Chromium itself. The Linux fetcher support, engine archive publishing and Ubuntu job below are planned work, not implemented or verified by this port.

1. Compile Chromium plus the pinned Helium Linux integration on the Linux build server. Key the engine artifact by the first 12 characters of `git rev-parse HEAD:engine`, and publish `ArdiaCore-<short-tree-sha>-linux-x64.tar.xz` plus its `.sha256` checksum. Define and verify the runtime payload needed by the Linux host (`chrome` and its resources, plus `libardiacore.so` if a separate library is produced). The existing application package from `scripts/linux/package.py` is not this engine-only archive.
2. Extend `fetch-engine.sh` with `--arch linux-x86_64 --nearest`. It should walk `git rev-list HEAD -- engine` for an exact or nearest available engine release, verify SHA-256 before use, and unpack into `apps/browser/build-ardiacore/prebuilt-linux-x86_64/<short-tree-sha>/`. Preserve the current macOS archive names (`ArdiaCore-<short>.tar.xz` and `ArdiaCore-<short>-x64.tar.xz`) and behavior.
3. Add an `app-linux-x64` job on `ubuntu-latest` alongside the existing macOS jobs. Cache `apps/browser/build-ardiacore/prebuilt-linux-x86_64` with `actions/cache@v6`, run `packages/ardiacore/scripts/fetch-engine.sh --arch linux-x86_64 --nearest`, and use `ARDIACORE_PREBUILT_REPO=mckamyk/ardia` as the repository fallback until `mantrakp04/ardia` hosts Linux archives.
4. Build fresh `apps/demo/dist` with `pnpm --filter @ardia/demo build`, package it with the fetched Linux engine and runtime resources, and upload `Ardia-linux-x64-${{ github.sha }}.tar.xz` using `actions/upload-artifact@v7`. Add the appropriate checks and validate installation and launch of a real package before calling this job working.

A roughly three-minute Ubuntu job is the intended goal once engine archives are available; no Linux fetch, cache, CI duration or runtime support has been established by this blueprint.

## Verification

Focused checks that do not need a Chromium checkout:

```bash
pnpm typecheck
pnpm --filter @ardia/demo test:host
pnpm --filter @ardia/demo build
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/patches/build/tests -v
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/chromium/src/chrome/browser/ardia/tests -v
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s engine/ardiacore-linux/tests -v
node --test engine/ardiacore-linux/tests/bootstrap.test.mjs
```

The tooling tests exercise platform selection, independent bases, repeat application, materialization, drift detection, shared lifetime retention and package layout. Engine fixtures compile the real dispatcher against test services, compile the C header, and verify Linux reauthentication never grants a test bypass. Browser fixtures verify host UI events, viewport hiding and session restoration, but use a simulated native host.

A scratch audit applied all selected Linux patch sections strictly against exact Chromium 155 source files after the relevant pinned Helium changes and domain substitution. This proves patch contexts, not GN dependency correctness, compilation, linking or runtime behavior.

Before claiming Linux support, verify a real build and package, restart persistence, last-tab behavior, keyboard focus and shortcuts, close/adopt/transfer paths, native overlay clipping, permissions and browser dialogs, extensions, downloads, sandboxing, crash shutdown and Wayland operation. Re-run the Mac checks for shared-code regressions.

## Features requiring Linux evidence

- Widevine requires a compatible CDM, supported discovery and successful service playback. `enable_widevine=true` and disabled host verification are build settings, not evidence that a streaming service works. Linux disables the Mac/Windows/ChromeOS-only CDM storage-ID implementation and RLZ support; the Mac storage key and settings remain unchanged.
- 1Password, passkeys, native messaging and hardware security keys require installation and browser-integration checks. Helium's native messaging fallback paths must be checked against the pinned patch, not assumed from Chromium profile conventions.
- Secret Service or KWallet storage needs encryption and restart checks on the target desktop. macOS Keychain helpers do not establish a Linux implementation.
- VA-API and PipeWire are enabled in Linux flags. GPU/driver decoding, codec support and Hyprland portal screen sharing still need runtime checks.
- Folder sync is not implemented by setting `ARDIA_SYNC_DIR`. The current sync module contains Swift/CryptoKit and native storage integration and needs a Linux implementation.
- Apple Translation, VideoToolbox and Metal features remain platform-specific. No AI features are planned.
