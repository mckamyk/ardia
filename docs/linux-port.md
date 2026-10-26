# Ardia on Linux

The Linux port targets Arch Linux, Omarchy, Hyprland and Wayland. Its desktop application lives in `apps/linux`, reuses the browser UI from `apps/browser` and Chromium services through `engine/chromium`, and has a separate Aura host in `engine/ardiacore-linux`.

The x64 Chromium executable has been compiled and exercised with Ardia's native host. Verification below covers headless startup, native-tab rendering and the extracted application package. Desktop integration and full feature parity remain under verification.

## Upstream merge objective (`Closes #1`)

The upstream owner, `@barre_of_lube` (`mantrakp04`), requested a working Linux build and CI before merging the port into [`mantrakp04/ardia`](https://github.com/mantrakp04/ardia). The upstream PR must include `Closes #1` for [Linux Support](https://github.com/mantrakp04/ardia/issues/1), pass the linked-issue check, and have the CLA signed on its PR thread when prompted by `.github/workflows/cla.yml`.

Keep macOS behavior and its CI intact. Linux additions belong behind `is_linux` in GN, with the Aura host in `engine/ardiacore-linux`. The desktop entry requires `window.__ardiaHost`; `apps/demo` remains the independently built ardia.dev web demo.

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

## Desktop application boundary

`apps/linux` owns the desktop entry point, window controls, current-space header, native utility-window startup and Linux component adapters. Build it with `pnpm --filter @ardia/linux build`; its output is `apps/linux/dist`. It uses the existing browser and Settings components rather than maintaining a second browser UI.

`apps/demo` keeps its own website entry point, seeded session and iframe behavior. Linux owns its Vite configuration, web rendering adapters, native module/event bridge, Trusted Types symbol rendering, host persistence and verification fixtures under `apps/linux`. It imports shared UI from `apps/browser` and packages without depending on `apps/demo`. Its build aliases select the native-backed WebView and Linux-only component adaptations, including the flat pinned tiles. Demo sources and configuration match `main`.

Linux behavior should follow macOS unless a platform constraint requires a difference. In particular, Settings and Import open in separate native utility windows, reuse an already open window, and leave the source browser's tabs alone. Utility renderers synchronize their edits against the canonical browser session without creating browser tabs or window records of their own.

Ardia draws its window buttons inside the application, alongside the selected space and a Settings button. The buttons request native close, minimize and maximize/restore operations. They do not depend on the compositor supplying a titlebar, so the same app controls are available on decorationless desktops such as Hyprland. The compositor still decides how native window requests interact with tiling and placement.

The host overlays Chromium's client view; it does not replace its outer nonclient frame. Chromium can therefore retain an additional system or custom titlebar. On Wayland, absence of the decoration protocol forces Chromium's custom frame; when server decorations are available, regular windows follow Chromium's custom-frame preference while the current legacy app utility windows use the native frame. X11 defaults depend on the window manager. Headless page captures exclude this outer frame, so a single controls row and native dragging/resizing still need compositor-level verification.

Settings opens through the header button or Ctrl+comma, including when a native web page has focus. It starts at 820×640, remembers its native placement and supplies a 720×480 minimum to the window manager. Import starts at 640×600 with resizing and maximization disabled. Programmatic DevTools bounds changes can bypass window-manager minimums. Normal utility close requests wait for pending session writes before acknowledging native closure; a save failure leaves the utility open for retry. Forced process termination cannot provide that guarantee.

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

Chromium's Linux prerequisites, a compatible Node/pnpm installation, Python, Git, util-linux `flock`, and ImageMagick or Pillow must be provisioned first. Helium's Docker build environment is a useful reference for dependencies. The x64 source preparation, pinned Helium integration and complete executable build were exercised on the dedicated builder described below.

```bash
export AC_CHROMIUM_BUILD="$HOME/ardia-linux-build"
export AC_TARGET_OS=linux
export AC_TARGET_CPU=x64
export PYTHONDONTWRITEBYTECODE=1
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
pnpm --filter @ardia/linux build
python3 scripts/linux/package.py \
  --out "$CB/chromium_git/chromium/src/out/$GN_OUT_CONFIGS" \
  --ui apps/linux/dist \
  --destination "$CB/artifacts/ardia-linux-$AC_TARGET_CPU.tar.xz"
```

The archive contains `bin/ardia`, `lib/ardia/chrome`, Chromium runtime resources, the `ardia-ui` bundle and desktop integration files. Launch the extracted `bin/ardia`; the raw build-tree `chrome` needs `--ardia-ui-dir` pointing to `apps/linux/dist` because that output directory does not contain the application UI. A real package was extracted and launched in the verification below. Run tests with a throwaway `--user-data-dir`, never an existing browser profile. Keep Chromium's sandbox enabled.

## Native x64 build evidence (2026-10-26 UTC)

The dedicated build root was `/home/mac/ardia-linux-build`. The checkout was based on `4101c2b366aef174fddbb026d04e79ecc2af0b4d`, with the uncommitted Linux compilation and preparation fixes described here. The committed engine tree was `5242855102f952fca7b477ee6f4582583c1ceb4a`; this is not an identifier for the additional working-tree fixes, and no engine archive was published.

Chromium was `155.0.8059.84` at `5869fe313571d75152b77813cf0a4ee26a4e6df0`, with Helium Chromium `832faa7822566270e10c0d3262f702f9d0fea81f` and Helium Linux `cf878d4aaff58d063beada276c437ae756a142aa`. The build retained the official release configuration, phase-2 PGO, ThinLTO, CFI and the Chromium sandbox. Six local compile jobs ran on an eight-CPU machine with 24 GB RAM and 46 GB swap.

The real preparation uncovered and fixed these issues:

- A fresh `depot_tools` checkout needed its Python bootstrap before GN and autoninja could run with automatic updates disabled.
- The official Debian sysroot contained hard-linked driver libraries. Linux preparation now replaces those regular files with private copies under the Chromium lock, preserving their contents and keeping the alias guard intact. It also removes Chromium's empty search-assets gitlink directory; a populated directory is rejected rather than deleted.
- Helium's Linux default-browser message contained a `helium.desktop` placeholder example. Linux branding now replaces it with `ardia.desktop` before the naming check.
- Reapplying overlapping scheduler patches failed because later patches changed the earlier reverse-check context. Linux application now recognizes files that exactly match the fully materialized final series; unexplained differences still follow the existing apply path.
- The native host needed Chromium 155 metadata declarations, public access restored after the metadata macro, an explicit URL path string conversion, its inherited accelerator interface and Chromium's native tab-observer registration and cleanup.
- Linux autofill no longer references a platform-unavailable authentication prompt string. Card-number access keeps returning the existing OS-authentication-unavailable error on Linux; the non-Linux implementation is preserved.

All 32 production Ardia host and service objects compiled. The subsequent full `chrome` invocation completed 44,510 build steps in `5h1m02.02s`, including the final native executable link, with `BUILD_EXIT=0`:

```bash
export AC_CHROMIUM_BUILD=/home/mac/ardia-linux-build
export AC_TARGET_OS=linux
export AC_TARGET_CPU=x64
export PYTHONDONTWRITEBYTECODE=1
JOBS=6 KEEP_GOING=1 timeout -s TERM 43200 scripts/linux/build.sh --apply
```

The resulting `out/Release_GN_linux_x64/chrome` is a 528,592,864-byte x86-64 ELF PIE with build ID `749fe824d8eb45cbd0cf14395d14f99d1ab189c8`. All dynamic dependencies resolved on the builder. `--version` returned `Chromium 155.0.8059.84 (Chromium 155.0.8059.84)`. Its SHA-256 is `2a72597a6a94277add923e7ce41a313c9dd5e45c5c53f2def8a5e564ecd516f2`.

Before the desktop entry was separated from the demo, the UI was built with `npm exec --yes --package=pnpm@11.1.3 -- pnpm --filter @ardia/demo build`. `scripts/linux/package.py` packaged the real executable, runtime resources and that UI into `artifacts/ardia-linux-x64-native-20261026.tar.xz` (143,027,892 bytes; SHA-256 `2ac7b28e0e201ec1b34d8ae5b9aeae337e4e6fccf47930331372154e4ba1aaac`). Extraction into `artifacts/install-check` preserved the executable hash. The extracted launcher loaded its bundled `chrome://ardia/` UI without a development asset override.

Runtime checks used throwaway profiles, `--ozone-platform=headless`, software rendering and no display-server connection. Extensions were disabled for the isolated checks because an initial scratch launch discovered the machine's externally installed 1Password extension. The test profiles used `--password-store=basic` and an isolated D-Bus address. No sandbox-disabling switches were used. Ardia's bridge reported `appBuild=linux-host`, the correct version and the scratch profile directory. It created a real Chrome tab, attached its native viewport and rendered a data page titled `Ardia native build verified`; that ordinary page had no `__ardiaHost`. A renderer reported `NoNewPrivs=1`, `Seccomp=2` and nested PID namespaces. Card-number access returned the expected authentication-unavailable error. Test instances closed through `Browser.close`.

The final strict series check passed all 56 steps and reproduced all 193 touched paths. Codex reviewed the patch-application and observer adaptations before and after their changes, with no unresolved findings. Evidence remains under the build root: `logs/native-build-05.log`, `logs/native-objects-03.log`, `logs/final-series-check.log`, `logs/runtime-native-tab.json`, `logs/runtime-page-visible.json`, `logs/runtime-package-host.json`, `logs/sandbox-status.json`, `logs/native-shell-large.png` and `logs/native-page.png`.

This establishes a real compiled and packaged browser with a functioning native host, not complete Linux parity. The UI still reports unsupported `ArdiaExtensions.searchEngineList`; the initial 1Password integration rejected the unsigned browser. D-Bus, keyring integration, native desktop/window behavior, Wayland, GPU acceleration, extensions, networking and broader lifecycle/session behavior require separate verification. The headless backend initially chose a small window; the UI and tab captures used explicitly resized windows. No macOS build or full feature suite was run on this Linux builder.

## Desktop header and utility verification (2026-10-26 UTC)

The dedicated desktop entry and native utility changes were verified against `5711a8497c105d6a1d1b2835fe1be9adad0ae550` plus the working-tree changes. The final Linux bundle is `apps/linux/dist/assets/index-DuAm1ljr.js`. Linux and demo builds passed, all nine workspace typechecks passed, and the focused session/utility checks passed 50 tests. Native checks passed seven fixtures and seven bootstrap tests. The available browser suite passed 782 tests with one existing skip, core passed 81 tests, and both native PiP programs passed. The full workspace test command still requires Swift, which is absent on this Linux builder.

The final production executable is 528,608,912 bytes, SHA-256 `a7f443bf90b808f454b95748ad6f1ebc9c87255a1a76d808d17b32d502ec0a0b`, build ID `547f16d7774a91beceefbd20feb9a6654b0bb010`. Earlier ordinary incremental builds completed the header and utility changes. A separate compiler-cache experiment subsequently regenerated the shared C++ module and invalidated Siso's dependency timestamps, even after its compiler-wrapper configuration was reverted. The final close correction was therefore compiled using the exact GN-generated production host command and linked using the complete GN-generated Chrome command and 23,113 ordered response-file inputs, under the Chromium flock. Host compilation took 10.37 seconds and the real final link took 206.11 seconds; both succeeded.

The recovery audit checked 153,895 source files and 79,473 generated inputs against retained pre-close build state, hashing all 792 metadata-changed inputs. Only the intended `ac_linux_host.cc` content differed. The toolchain and configuration matched, the added GN dependency was already present in the successful utility build, and all 289 subsequently touched link artifacts were byte-identical to their previous contents. No build-state metadata or timestamps were changed to mark dependencies clean. Siso's invalidation remains unresolved: an ordinary future build may still schedule broad recompilation. Retain `logs/header-settings-native.log`, `logs/header-settings/native-close-input-audit.json`, `native-close-command-provenance.json` and the recorded compile/link commands for provenance.

Isolated native checks used the headless Ozone backend with an explicit 1920×1080 screen and the Chromium sandbox enabled. The header showed Personal, three functional controls and Settings. Ctrl+comma from a focused real native page opened Settings; repeated opens reused it, pane navigation worked, and its 900×700 resized placement survived closure and reopening. Updating a setting and renaming the profile, followed immediately by native Ctrl+W, persisted both changes. The source page retained its target, time origin, active-tab identity and running counter. Minimize and maximize/restore changed the native window state. Source-window close requests, the utility's close button and Escape also saved before closure. Forced dark appearance rendered correctly; Import received its selected source, kept browser session tables unchanged and disabled maximize.

Synthetic Alt+F4 did not reach Chromium's close accelerator in the headless shell, so compositor-driven close is covered by source inspection and the compiled close-interceptor fixture rather than a desktop runtime claim. Window-manager minimum sizing, native decorations, dragging, Wayland/GPU behavior and tiling remain desktop checks. Evidence and captures are under `logs/header-settings`, including `after-flow.json`, `close-paths.json`, `final-header.png`, `final-settings.png`, `settings-forced-dark.png` and `import-native.png`. The native multi-profile restriction remains: the generic three-way session merge can retain a concurrently created tab referring to a profile another renderer deleted.

The new package is `/home/mac/ardia-linux-build/artifacts/ardia-linux-x64-header-settings-20261026.tar.xz` (142,992,332 bytes; SHA-256 `36789143e68721832d8d80d8c1ace87faad9e426aa67e18ad942ee20460b83a4`). It was extracted into `artifacts/header-settings` and its executable hash matched the real build output. The extracted `ardia/bin/ardia` launched with a fresh test profile and no UI override, loaded `chrome://ardia/assets/index-DuAm1ljr.js`, displayed the current space and three controls, and opened/reused/independently closed its separate Settings window. Evidence is `logs/header-settings/packaged.json` with `packaged-header.png` and `packaged-settings.png`. All test instances were closed; the earlier installed bundle was not replaced.

Run the new extracted application with:

```bash
/home/mac/ardia-linux-build/artifacts/header-settings/ardia/bin/ardia
```

## Patch maintenance

Manifest entries are `chromium <patch> [mac|linux|all]` or `layer <directory> [mac|linux|all]`. Omitted selectors mean shared. Apply, capture-base, materialize, files and check all use the same selection.

Mac arm64 retains the existing `series-base`. Other targets use `series-base-<platform>-<cpu>`, and recorded platform metadata prevents reuse across incompatible targets. The Linux layer's shared headers are included in capture, materialize and drift checks even though their source lives in the Mac layer directory.

Do not classify a patch solely by its filename. For example, `chromium-window-hosted.patch` retains shared last-tab/window lifetime changes, while its Cocoa view behavior lives in `chromium-window-hosted-mac.patch`. CEF carryover and Helium branding also have shared and platform-specific sections.

The Web Store patch already derives OS and CPU from Chromium's build. Do not replace a hardcoded `os=mac` string; none is used for its platform query.

## Upstream synchronization

`check-upstream.sh` and `sync-upstream.sh` wrap `scripts/linux/upstream.py`; both accept `--help`, `--ref`, `--no-fetch` and `--json`. By default they fetch `upstream/main`, adding the `upstream` remote if absent and refusing to fetch an existing remote that points outside `mantrakp04/ardia`. Use `--no-fetch` with an existing local ref for a read-only report without changing remotes or fetching.

The report compares the merge base with the selected upstream ref. `ENGINE_FULL` means actual Chromium version or selected Chromium/Helium pin identities changed; editing those files without changing the identities does not itself require a full rebuild. Ordinary engine, Linux host, patch or Linux tooling edits produce `ENGINE_INCREMENTAL`. Linux/shared browser UI and workspace dependency changes produce `UI_ONLY`; independent demo and unrelated changes produce `NONE`. JSON and human output include `engine_tree`, the first 12 characters of `git rev-parse HEAD:engine`, even when upstream is already incorporated. This identifies the current committed engine tree, not the incoming tree or uncommitted edits.

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
pnpm --filter @ardia/linux build
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
4. Build fresh `apps/linux/dist` with `pnpm --filter @ardia/linux build`, package it with the fetched Linux engine and runtime resources, and upload `Ardia-linux-x64-${{ github.sha }}.tar.xz` using `actions/upload-artifact@v7`. Add the appropriate checks and validate installation and launch of a real package before calling this job working.

A roughly three-minute Ubuntu job is the intended goal once engine archives are available; no Linux fetch, cache, CI duration or runtime support has been established by this blueprint.

## Optional build acceleration (planned)

Compiler caching is an optional strategy for future local Linux builders and engine-release jobs. It is not configured or verified by this port. Per-commit app CI should continue consuming prebuilt engines rather than compiling Chromium on GitHub-hosted runners.

For a dedicated development builder, install `sccache` (for example, `sudo pacman -S sccache` on Arch) and choose a local cache budget:

```bash
export SCCACHE_DIR="$HOME/.cache/sccache"
export SCCACHE_CACHE_SIZE="50G"
```

A future compiler-wrapper integration can set `cc_wrapper="sccache"` in that builder's GN configuration. `gn_defines.sh` does not currently detect or enable it automatically. Cloudflare R2 or S3 can provide a shared backend through `SCCACHE_BUCKET`, `SCCACHE_ENDPOINT` and `SCCACHE_REGION=auto`. Configure credentials through the runner's secret mechanism. Cache compatibility and reuse must be measured before relying on the shared object cache.

Development builders with more compile CPUs than available link memory can evaluate `concurrent_links=1` and a separate non-ThinLTO configuration (`use_thin_lto=false`). These are development tradeoffs, not changes to the verified official PGO/ThinLTO/CFI build. Validate the complete flag combination separately and preserve the existing production output directory. Changing compiler wrappers or common module settings can invalidate the dependency graph and schedule broad recompilation.

For a future self-hosted engine-release workflow, evaluate `mozilla-actions/sccache-action@v0.0.9` with `SCCACHE_GHA_ENABLED=true` or an R2 backend. The planned per-commit Ubuntu app job instead caches fetched engine archives with `actions/cache@v6`, builds the fresh UI and packages it without compiling Chromium. The fetcher, job and roughly three-minute duration remain unimplemented targets described above.

## Verification

Focused checks that do not need a Chromium checkout:

```bash
pnpm typecheck
pnpm --filter @ardia/linux test:host
pnpm --filter @ardia/linux build
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

## Essentials styling

The Linux/web sidebar resolves `apps/browser/src/components/sidebar/TileFace.web.tsx` through the existing Vite web extension priority. Essentials use flat translucent fills and thin neutral borders, with separate hover, pressed, active and multi-selection states in both appearances. Their dimensions stay shared with macOS: 41 pt tall, 10 pt corner radius, 6 pt grid gaps and centered 16 pt favicons. Folder tiles use the same treatment and keep their existing badges and interactions. The native `TileFace.tsx` stays unchanged; the web variant reuses its drag positioning and folder drop ring. This deliberately avoids relying on AppKit glass or the incomplete web `DockSelection` shim for essential tiles.

## Context submenu positioning

The Linux menu renderer lives in `apps/linux/src/menus.tsx` and portals each menu to the document body. Linux's shell exports its menu API, its entry point renders its menu host, and its native-tab and shell adapters import that same menu instance. Menu visibility and subscriptions therefore follow the Linux renderer, including native-tab overlay hiding. The website demo retains its existing renderer and styles.

Previously, the menu backdrop filter made the parent menu the containing block for a fixed-position submenu, while submenu offsets were calculated in viewport coordinates. A menu at (120, 100) reproduced a submenu displaced vertically by 100 px with a 106 px horizontal gap. Submenus now anchor to the actual parent-menu bounds and hovered row, with a 4 px overlap and a 5 px top inset. They flip left at the right viewport edge; measured menus stay within 8 px of each viewport edge and scroll when too tall. Resize and captured scroll events update open menus. Linux's stylesheet contains the expanded-row highlight and clips icons' oversized transparent SVG canvases so short menus do not acquire a spurious scrollbar.

Verification passed all nine workspace typechecks, both Linux and demo production builds, and 50 host/session tests. A fixture using Linux's Vite configuration confirmed the shell and shared native-tab adapter use the Linux renderer's menu state, including visibility subscriptions. Headless browser checks exercised three menu levels, leaf selection, disabled entries, replacement, dismissal, viewport edges, resizing and scrolling. The existing packaged Linux engine loaded this checkout's production UI with a throwaway profile and confirmed a real pinned tab's context submenu aligned exactly, remained open while the pointer entered it and closed with Escape. Evidence is under `/tmp/ardia-submenu-offset`, including `linux-only-verification.json` and `native/actual-tab-submenu.png`. No demo, engine or macOS source changed.


## Linux and demo ownership audit (2026-10-26)

The branch comparison against `main` found 19 Linux host/configuration/test additions and 12 modified files still under `apps/demo`. All 19 additions now live under `apps/linux`; the 12 existing demo files have been restored byte-for-byte to `main`. The Linux flat-tile adapter also moved out of `apps/browser/src/components/sidebar/TileFace.web.tsx` into `apps/linux/src/TileFace.tsx`, selected only by Linux's Vite resolver. Shared browser source therefore retains its original behavior for demo and macOS.

Linux now owns its rendering adapters and base styles as well as its entry point. Its native module providers and event listeners resolve to one Linux Expo adapter, its symbol renderer owns one Trusted Types policy, and its native-tab, utility and shell adapters use the same menu state. Window controls and their position updates share one Linux window-chrome module. Host, session, symbol and CSP verification commands are available from `@ardia/linux`; no Linux imports or configuration depend on `apps/demo`. The lockfile already contains the Linux workspace dependencies and needs no dependency changes for this relocation.

`scripts/linux/upstream.py` now recognizes `apps/linux/` changes as `UI_ONLY`, with coverage for entry, host and Vite changes. Independent demo changes produce `NONE`. Build and packaging instructions target `apps/linux/dist`. The earlier demo-built package evidence above describes the historical build; prebuilt-engine CI remains planned work.

Production module-graph assertions passed: Linux loaded 461 application modules with none from demo, and demo loaded 450 with none from Linux. Linux selected its flat-tile adapter while demo selected the original shared tile; Linux resolved exactly one Expo adapter, symbol renderer and menu instance.

Verification passed both production UI builds, all nine workspace typechecks, 50 host/session tests, four symbol tests, 15 platform-tooling tests, five shared-engine Linux checks, seven native-host fixture checks and seven bootstrap tests. The moved CSP fixture rendered all 278 symbols under the host policy and verified rejection of raw HTML, unapproved or duplicate policies, and string evaluation. The restored demo booted with 12 seeded tabs, two profiles and its iframe renderer, even with a host-shaped global present.

The existing packaged Linux executable loaded the migrated production bundle with fresh profiles, the headless Ozone backend and Chromium's sandbox enabled. Checks covered native window controls, real-tab navigation events, absence of the privileged bridge in ordinary pages, native context submenus, reusable Settings windows and save-before-close synchronization without changing browser tabs or windows. No engine sources or binaries were changed for this relocation. Wayland/compositor behavior and a macOS application build were not exercised. Evidence is under `/tmp/ardia-linux-boundary-review`.

The final review found and reproduced an Escape propagation bug in Settings dropdowns. Linux's menu handler now consumes Escape before dismissing the menu, so the utility-window handler cannot close Settings during that same event. Native keyboard verification confirmed that the first Escape dismisses the dropdown while retaining Settings and a second Escape closes Settings normally. Linux typecheck and production build passed again after the correction.
