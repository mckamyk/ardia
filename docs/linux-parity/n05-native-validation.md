# N05 launch switches and native validation

This lane addresses source and tooling repairs for issue [#42](https://github.com/mckamyk/ardia/issues/42). It does not close native acceptance. Starting commit `d4df45f7` contains the required Linux base `b70711df2af16cee08146b78f7d3405c57d6d1de` and upstream `a2ca6bdf`.

## Implemented contract

Linux materializes its own `ardia/core/BUILD.gn` and `ac_launch_switches_linux.cc` alongside the unchanged shared `ac_launch_switches.h`. The existing services dependency on `//ardia/core:launch_switches` resolves to a small source set with base, Chrome constants and content constants dependencies. It has no Linux host dependency or Apple frameworks. The macOS source selection and implementation remain unchanged.

`ardia/linux/ac_linux_launch_config.h` declares:

```cpp
void ardiacore::InitializeLinuxLaunchConfig();
base::DictValue ardiacore::LinuxLaunchConfig(
    const base::FilePath& data_directory,
    const base::FilePath& user_data_directory);
```

The host integration owner calls initialization at the beginning of `StartLinuxHost`, before reading switches, deciding the test gate or creating privileged bootstrap data. Then call `ardiacore::DecideTestGateAtStartup()`. Initialization is idempotent and browser-only. Use `BackgroundInstance()` for the host's background decision while preserving its headless checks.

Legacy environment normalization lives only in this helper. Explicit command-line switches take precedence, including empty values. `ARDIA_BACKGROUND=1` supplies `--ardia-background`; `ARDIA_SWITCHES` supplies `--ardia-switches`; `ARDIA_INSTANCE` recognizes only `downloadsDir` and supplies `--ardia-downloads-dir`. Other instance/environment names are not copied. `ARDIA_DATA_DIR` cannot redirect a profile after Chromium has selected and opened it, so this late initializer does not consume it. Select scratch storage with `--user-data-dir` before startup.

The bootstrap owner sets `launchConfig` from `LinuxLaunchConfig(DocumentDirectory(profile), actual_user_data_directory)`, with the second argument read from native `chrome::DIR_USER_DATA`. These are native paths, never renderer-supplied claims. `dataDirectory` is the actual document root, or null when absent. `isolated` requires an explicit nonempty `--user-data-dir` whose canonical existing path equals the actual native user-data directory. Background/headless state and an `--ardia-data-dir` argument alone confer no isolation or test authority.

The snapshot contains exactly `dataDirectory`, `background`, `isolated`, `testInstance`, `instance`, `switches`, `trace` and `test`. It exposes only the supported `downloadsDir` instance value. Boolean switch parsing uses the same native implementation as `SwitchOverride`; defaults remain on for kill switches and off for experiments. Linux unconditionally keeps `newTabPrewarm` off, including explicit attempts to enable it. GPU propagation copies only the shared switch override argument to GPU processes; no test, trace or instance values propagate through that API.

Linux cannot establish the shared distribution-signing/mock-keychain test gate. Consequently `SignedForDistribution()` conservatively returns true, `TestInstance`, `TestFlag` and `Traced` return false, and `TestValue` returns no value. Bootstrap `test` and `trace` are empty. Neither background/headless flags nor scratch profiles or mock-keychain flags enable authentication bypasses. Existing Linux OS reauthentication remains unavailable and denies requests.

## Verified on 2026-10-26

- Baseline tooling reproduced the stale Chromium 155 assertion: 14 passed, one failed.
- `python3 -m unittest discover -s engine/patches/build/tests -v`: 19 passed after repair. Covers matching environment/pin versions and tags, rejecting a 155 tree and mismatched actual Linux submodule, both platform branding fixtures, platform manifests, package fixtures and Linux launch-file materialization/drift detection.
- `python3 -m unittest discover -s engine/ardiacore-linux/tests -p test_launch_switches.py -v`: eight passed. Compiles the real Linux implementation with C++20 fixture headers and exercises defaults, denied test authority, CLI/environment precedence, duplicate/malformed values, child-process environment isolation, canonical path matching, null roots and GPU propagation scope. These fixture headers are not Chromium 156 header verification.
- Standalone GN generation of the materialized Linux launch target succeeded using the installed GN tool and fixture dependency targets: five targets, six files. `gn desc` resolved only `//base`, `//chrome/common:non_code_constants` and `//content/public/common`; the selected sources are the Linux implementation, shared declaration and Linux helper header. This establishes the target's graph/syntax in isolation, not full Chromium 156 GN compatibility.
- `bash -n engine/ardiacore-linux/apply.sh engine/patches/build/env.sh` and `git diff --check` passed.

Independent review found that legacy `ARDIA_INSTANCE` parsing truncated download paths at commas. A compiled regression reproduced the failure before correction. Linux now retains the shared comma-continuation grammar until the next recognized instance key, while exporting only `downloadsDir` and retaining explicit CLI precedence. The launch fixture now passes all ten cases, including comma-containing environment and CLI paths with spaces, embedded equals signs and empty segments. This correction adds no test or authentication authority.

The stale `HELIUM_LINUX_TAG` now matches the pinned Linux `0.20.1.1`; Chromium remains `156.0.8078.47`, shared Helium `0.20.1` at `f29a25968e483f8bb85e47829ccd487bbe229199`, and Linux Helium `473c0f72d7db2e8492f0fa975b8544bc719bb027`.

## Outstanding native acceptance

Read-only inspection found only `/home/mac/ardia-linux-build/chromium_git/chromium/src`, with `chrome/VERSION` reporting `155.0.8059.84`. Its Helium checkouts do not contain the selected 156 pin objects. There is no `/home/mac/chromium-build` tree or matching 156 source/output identified. The filesystem had 431 GiB available. No old source/output was changed; no engine download, source sync, GN generation against that tree, native build or old executable qualification was attempted.

A separately provisioned source root such as `/home/mac/ardia-linux-build-156/chromium_git/chromium/src` must contain complete Chromium `156.0.8078.47` dependencies and the exact clean Helium commits above. Provisioning/building a fresh tree is an hours-long operation, with approximately 80–150 GiB or more of source, dependencies and build storage to budget. It requires separate coordination; do not run `gclient sync`, `gn clean`, or reuse/clean the 155 outputs.

With matching sources already provisioned, the next read-only compatibility check is:

```sh
AC_TARGET_OS=linux AC_TARGET_CPU=x64 AC_CHROMIUM_BUILD=/home/mac/ardia-linux-build-156 CHROMIUM_SRC=/home/mac/ardia-linux-build-156/chromium_git/chromium/src python3 engine/patches/build/03-helium.py --platform linux --cpu x64 --only pins,guard
```

Then apply the selected Helium stages and Ardia series to that version-correct tree, perform strict `series.py check --platform linux --cpu x64`, and run full GN generation/header checks. Any materialization or incremental build must hold the existing real Chromium lock, `/tmp/ac-chromium.lockf`; on Linux use the repository's flock adapter or `flock -w 60 /tmp/ac-chromium.lockf` around the build command, with the holder visible to `pathguard.py lock-held`. Do not create a separate lock namespace. Use `AC_TARGET_OS=linux AC_TARGET_CPU=x64 AC_CHROMIUM_BUILD=/home/mac/ardia-linux-build-156` with `02-gn-args.sh --apply` and `05-build.sh --apply` only against that correctly provisioned tree.

Issue #42 remains open for full selected 156 patch/hook/GN/header compatibility, a matching engine/package and ABI/bootstrap checks, Chromium sandbox and ordinary-page bridge isolation, plus migrated-UI keyboard, restored-session and auto-PiP acceptance with scratch profiles and recorded binary/UI/source identities. Host integration is supplied by the separate bridge lane. This lane has not exercised the combined host or current UI against a native engine.
