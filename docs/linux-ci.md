# Linux CI

Linux CI follows upstream's prebuilt-engine model. Chromium is compiled only on the Linux builder; GitHub Actions fetches a published, checksum-verified engine, builds the current `apps/linux` UI, packages it and smoke-tests the package. Hosted CI never runs `gclient sync`, GN or a Chromium build.

Status (2026-10-28): the workflow, fetcher, publisher, packaging allowlist and smoke gate exist and pass locally. Repository Actions are disabled in `mckamyk/ardia`, no Linux engine has been published, and no hosted run has happened yet.

## Pieces

| Piece | What it does |
| --- | --- |
| [`.github/workflows/linux.yml`](../.github/workflows/linux.yml) | Manual (`workflow_dispatch`) workflow with a `checks` job and a `package` job, both on `ubuntu-24.04` |
| [`.github/actions/linux-setup`](../.github/actions/linux-setup/action.yml) | Ubuntu packages for the fixtures (clang, dbus, NSS tools, gnome-keyring, PyGObject), Node 24, pnpm and `pnpm install --frozen-lockfile` |
| [`scripts/linux/ci-checks.sh`](../scripts/linux/ci-checks.sh) | The engine-free checks; the `engine/ardiacore-linux/tests` modules run in parallel because the suite takes well over five minutes serially |
| [`scripts/linux/publish-engine.sh`](../scripts/linux/publish-engine.sh) | Builder side: stages, strips and archives an engine from a Chromium out dir; `--upload` publishes it and adds its table row |
| [`packages/ardiacore/prebuilt-engines-linux.tsv`](../packages/ardiacore/prebuilt-engines-linux.tsv) | `tree<TAB>tag<TAB>sha256` for each published Linux engine; empty until the first publish |
| `packages/ardiacore/scripts/fetch-engine.sh --arch linux-x86_64` | Dispatches to [`scripts/linux/fetch-engine.sh`](../scripts/linux/fetch-engine.sh); the Mac path is unchanged |
| [`scripts/linux/runtime_files.py`](../scripts/linux/runtime_files.py) | The one runtime allowlist used by publishing, fetching and packaging, and the safe archive unpacker |
| [`scripts/linux/smoke.mjs`](../scripts/linux/smoke.mjs) | Strict smoke gate for a packaged tarball |

## Engine archives

The key is `git rev-parse HEAD:engine`, as on the Mac. `ArdiaCore-<tree12>-linux-x64.tar.xz` holds one directory, `ardiacore-linux-x64/`, with the allowlisted runtime files and a `manifest.json` (engine tree, source commit, Chromium version, `args.gn` hash, chrome hash and build ID, build and packaging times). It is attached to an `engine-<tree12>` prerelease in `$ARDIACORE_PREBUILT_REPO` (default `mckamyk/ardia`) with a `.sha256` beside it.

The allowlist ships only what Chromium loads at runtime. The old packaging globs also shipped `.so.TOC` build files, the mock Vulkan driver, the 28 MB Vulkan validation layer and the `locales/*.pak.info` build files. The publisher strips a staging copy (`symbol_level=0` builds have no debug symbols to keep): chrome goes from 535 MB to 296 MB and the archive is 112 MB.

The publisher never trusts `repo.commit` alone. It computes the git tree of the source copy's `engine/` and records `source_commit` only when that commit's `engine/` tree matches. It copies from the out dir while holding the chromium lock and never writes there.

Publishing an engine for the current build:

```bash
scripts/linux/publish-engine.sh \
  --out ~/ardia-linux-build-156/chromium_git/chromium/src/out/Release_GN_linux_x64 \
  --source ~/ardia-linux-build-156/repo --upload
git add packages/ardiacore/prebuilt-engines-linux.tsv
```

Without `--upload` it only writes the archive and prints its table row. The 2026-10-28 dry run for engine tree `2f4dea0592e0` matched `80578594:engine`.

The fetcher verifies the SHA-256 before unpacking, rejects entries outside `ardiacore-linux-x64/`, links pointing outside it and special files, checks the manifest's tree and platform, then moves the engine into `apps/browser/build-ardiacore/prebuilt-linux-x86_64/<tree12>/` and marks it complete last. It reads the local table, then the cache, then the published table on the `linux` branch, so CI on an older commit still finds engines published later.

## Nearest engine for CI, exact engine for releases

`engine/` changed in 328 commits over the 30 days before 2026-10-28, 82 of them Mac-only. Requiring an exact engine would leave the package job red after almost every merge. The workflow therefore defaults to `--nearest`, like the old `build.yml`: the job warns, the summary names both trees, and the artifact is named `Ardia-linux-x64-<commit12>-engine-<engine12>`. Choose `exact` when a package must match its source, which is what a Linux release will require. Publishing after each `engine/` change on the builder keeps nearest builds close.

## Jobs

`checks` needs no engine: the `apps/linux` typecheck, host, symbol, module-graph and acceptance self-tests, the UI build, and the Python and Node suites under `engine/patches/build/tests`, `engine/chromium/src/chrome/browser/ardia/tests` and `engine/ardiacore-linux/tests`. On failure it uploads every check's log.

`package` fetches the engine (cached per tree and table), builds `apps/linux/dist`, prepares uBlock Origin with `scripts/linux/fetch-ubo.sh` (cached), runs `scripts/linux/package.py --out <engine>`, then runs the smoke gate and uploads the tarball, its SHA-256 and `results.json` for seven days. Its apt list covers the libraries `chrome` links (`readelf -d`) plus GTK and libsecret, which it loads at runtime.

The Ubuntu compatibility risk is small: the engine builds against Chromium's own sysroot (`use_sysroot=true`), and `chrome` needs at most GLIBC 2.25.

## Smoke gate

`node scripts/linux/smoke.mjs --package <tar.xz> --results <dir>` extracts the tarball and launches `bin/ardia` with headless Ozone, SwiftShader, `ARDIA_BACKGROUND=1`, a throwaway profile and no `--ardia-ui-dir`. It refuses any sandbox-disabling switch. Every check must run and pass:

1. `ldd` resolves every library.
2. The `chrome://ardia/` shell renders, and its assets are byte-identical to the packaged UI.
3. The bootstrap reports `appBuild=linux-host`.
4. A native tab loads a local HTTP fixture, paints it, and the page cannot see `__ardiaHost`.
5. Every renderer runs under seccomp in its own user and pid namespace.
6. The shipped uBlock Origin and its Linux bridge are present and the content blocker reports ready.
7. A clean quit and relaunch restores the tab.
8. `install.sh --destdir` stages the launcher, desktop entry, chrome, UI and polkit policy with the right modes.

It takes about 8 s on the builder and writes `results.json` and the launch logs. Focus, GPU, keyring and desktop dialogs stay in the builder's acceptance runs; a headless smoke doesn't cover them.

## Sandbox on Ubuntu

Ubuntu 24.04 limits unprivileged user namespaces through AppArmor (`kernel.apparmor_restrict_unprivileged_userns=1`), and the package ships no SUID `chrome_sandbox`. Chromium then refuses to start rather than run unsandboxed. The workflow lifts the restriction on its throwaway runner before the smoke step and never passes `--no-sandbox`. Users on Ubuntu 24.04 hit the same restriction; the package needs an AppArmor profile from `install.sh` (as Chrome's `.deb` ships) before it is usable there.

## Turning it on

1. Publish an engine with `publish-engine.sh --upload` and commit the table row.
2. Disable the workflows that should not run in the fork (`gh workflow disable Deploy -R mckamyk/ardia`, and Release), then enable repository Actions.
3. Run Linux from the default branch, inspect the artifact, and measure the jobs.
4. Then add `pull_request` and `push` triggers on `apps/linux/**`, `apps/browser/**`, `packages/**`, `engine/**`, `scripts/linux/**`, the workspace manifests and lockfile, `patches/**` and the workflow itself.
