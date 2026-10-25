# Ardia on Linux — Upstream Merge & CI Blueprint (`Closes #1`)

Upstream owner `@barre_of_lube` (`mantrakp04`) confirmed on X ([status/2114421942472393192](https://x.com/barre_of_lube/status/2114421942472393192) & [status/2114424716786384919](https://x.com/barre_of_lube/status/2114424716786384919)) that they want the Linux port merged upstream once the Linux build and CI are working cleanly.

This document is the blueprint for building the Linux target on **Omarchy (Arch Linux + Hyprland / Wayland)** and wiring up GitHub Actions CI so the PR can merge cleanly into [`mantrakp04/ardia`](https://github.com/mantrakp04/ardia) (`Closes #1`).

---

## 1. Upstream Merge Checklist (Must-Follow Rules from `CONTRIBUTING.md` & `AGENTS.md`)

1. **Zero comments in any source, script, or config file**:
   - Do **not** put `//`, `/* */`, `#`, or `<!-- -->` comments in `.cc`, `.h`, `.ts`, `.tsx`, `.sh`, `.py`, `.gn`, or `.yml` files.
   - Only shebangs (`#!/usr/bin/env bash`), SPDX/license headers (`// Copyright 2026 Ardia. Apache-2.0.`), tool directives (`@ts-expect-error`, `#pragma`, `# noqa`), and a script's `usage="..."` `--help` string are permitted.
   - Put all explanations in commit messages and `docs/`.
2. **Issue linking & CLA**:
   - The upstream PR description must include `Closes #1` ([Issue #1: Linux Support](https://github.com/mantrakp04/ardia/issues/1)) or `.github/workflows/linked-issue.yml` will fail.
   - Sign the CLA via the PR comment when prompted by `.github/workflows/cla.yml`.
3. **Pass all existing checks**:
   - `pnpm typecheck`
   - `pnpm test`
   - `python3 engine/patches/series.py check`

---

## 2. How Upstream's CI Works (and How Linux CI Must Match It)

Why did `@barre_of_lube` specifically ask to *"set up the ci once u get the linux build working"*?

Look at [`.github/workflows/build.yml`](../.github/workflows/build.yml) and [`packages/ardiacore/scripts/fetch-engine.sh`](../packages/ardiacore/scripts/fetch-engine.sh):
- **GitHub Actions does NOT compile Chromium** (a full Chromium build takes ~4–5 hours and ~100 GB of disk).
- Instead, upstream uses a **Content-Addressed Prebuilt Engine Archive** keyed by the Git tree hash of `engine/`:
  1. `tree=$(git rev-parse HEAD:engine)` and `short=${tree:0:12}`.
  2. When `engine/` is built on a dedicated machine (your Linux build server for Linux, or the owner's Mac for macOS), the compiled engine artifact is packaged as:
     - macOS arm64: `ArdiaCore-<short>.tar.xz`
     - macOS x86_64: `ArdiaCore-<short>-x64.tar.xz`
     - **Linux x86_64 (new)**: `ArdiaCore-<short>-linux-x64.tar.xz` (plus `.sha256`)
  3. `packages/ardiacore/scripts/fetch-engine.sh` walks `git rev-list HEAD -- engine` to find the exact (or `--nearest`) prebuilt engine release, verifies its SHA-256, and unpacks it into `apps/browser/build-ardiacore/prebuilt-linux-x86_64/<short>/`.
  4. In `.github/workflows/build.yml`, we add an `app-linux-x64` job (`runs-on: ubuntu-latest`) right next to `app-arm64` that:
     - Caches `apps/browser/build-ardiacore/prebuilt-linux-x86_64` via `actions/cache@v6`.
     - Runs `packages/ardiacore/scripts/fetch-engine.sh --arch linux-x86_64 --nearest` (falling back to `ARDIACORE_PREBUILT_REPO=mckamyk/ardia` until upstream hosts the Linux engine tarballs on `mantrakp04/ardia`).
     - Runs `pnpm --filter @ardia/demo build` to produce the `react-native-web` + WebGL UI bundle (`apps/demo/dist`).
     - Packages the prebuilt Linux ArdiaCore binary + the fresh UI bundle into `Ardia-linux-x64-${{ github.sha }}.tar.xz` and uploads it with `actions/upload-artifact@v7`.

That gives `@barre_of_lube` a **3-minute Linux CI job on `ubuntu-latest`** that rebuilds and tests the Linux app on every single commit without touching their macOS CI runners.

---

## 3. Architecture of the Linux Port (~90% Already in Repo)

| Component | Repo Path | Status | What Needs to Happen |
|---|---|---|---|
| **React 19 + Zustand Browser UI** | `apps/browser/src/`, `packages/core/` | **100% Done** | Zero changes needed. |
| **`react-native-web` + WebGL Shims** | `apps/demo/` (`vite.config.ts`, `src/shims/`) | **95% Done** | Already compiles `WindowRoot`, WebGL backdrop/tint shaders (`src/shims/web/gl/`), SVG SF Symbols (`src/shims/web/symbols/`), and DOM menus. Needs ~500 LOC in `src/shims/ardiacore.tsx`, `shell.tsx`, and `web/WebView.tsx` to talk to `window.__ardiaHost` when running inside the Linux desktop binary instead of the website demo. |
| **Engine C ABI** | `engine/chromium/src/chrome/browser/ardia/public/ac_engine.h` | **100% Done** | `AC_ENGINE_ABI_VERSION 2`: two plain C JSON functions (`ac_engine_call_t`, `ac_engine_set_event_sink_t`). |
| **Chromium C++ Services** | `engine/chromium/src/chrome/browser/ardia/ac_*.cc` | **92% Done** | 37 of 40 files are pure C++. In `BUILD.gn`, change `assert(is_mac)` to `assert(is_mac || is_linux)`, wrap the 3 `.mm` files (`ac_external_apps.mm`, `ac_input_frame_rate.mm`, `pip/ac_pip_mac.mm`) in `if (is_mac)`, and add small Linux `.cc` equivalents in `if (is_linux)`. |
| **Linux Window & Tab Host** | `engine/ardiacore/src/ardia/core/` | **New (~2,000 LOC C++)** | Add Linux Aura / `views::Widget` host files (`*_linux.cc` gated by `is_linux` in `engine/ardiacore/src/ardia/core/BUILD.gn`) that mount the `apps/demo/dist` WebUI bundle in a shell `views::WebView` and attach each tab's `content::WebContents` to a child `views::WebView` positioned at the `{x, y, width, height}` viewport rect reported by `WebView.tsx`. |
| **Helium Linux Patch Pipeline** | `engine/patches/build/` | **Small adaptation** | Pin `imputnet/helium-linux` in `helium-pins.ini`, support `target_os = ['linux']` in `01-sync.sh`, add Linux GN flags (`use_vaapi=true`, `rtc_use_pipewire=true`, `enable_cdm_host_verification=false`) in `gn_defines.sh`, and use ImageMagick/rsvg/Pillow instead of `sips` when `uname -s` is `Linux` in `03-helium.py`. |

---

## 4. Sanctioned & Platform Features on Linux (DRM, Passkeys, 1Password, Wayland)

1. **Widevine DRM (Netflix, Spotify, Prime Video, Disney+)**:
   - On macOS and Windows, Widevine enforces **VMP (`enable_cdm_host_verification=true`)**, requiring a commercial Google `.sig` signature on the browser framework.
   - On **Linux**, Google's `libwidevinecdm.so` **does not enforce VMP host verification** (`media/cdm/cdm_paths.gni` only enables it on `is_mac || is_win`).
   - With `proprietary_codecs=true`, `ffmpeg_branding="Chrome"`, `enable_widevine=true`, and `enable_cdm_host_verification=false` in `gn_defines.sh` (plus `chromium-widevine` from the AUR on Omarchy), DRM streaming works out of the box.
2. **1Password & Passkeys (WebAuthn)**:
   - Helium's `scan-chrome-native-messaging-hosts.patch` (already applied by Ardia) scans standard Linux Chrome/Chromium NativeMessagingHosts paths, so the 1Password Linux desktop app + extension unlock and autofill passkeys as soon as the binary is added to 1Password's Trusted Browsers list.
   - FIDO2 hardware keys work directly via `libudev` (`/dev/hidraw*`), and cookie/password encryption uses `libsecret` (`gnome-keyring` / `keepassxc`).
3. **Wayland / Hyprland & Hardware Video**:
   - `use_vaapi=true` and `rtc_use_pipewire=true` enable GPU video decode and native Hyprland PipeWire screen sharing.

---

## 5. Server Workflow: Upstream Sync & Prebuilt Engine Publishing

1. **Check upstream drift (`mantrakp04/ardia`)**:
   ```bash
   ./scripts/linux/check-upstream.sh
   ```
   Reports how many commits `mantrakp04/ardia` is ahead and classifies the delta into:
   - `UI_ONLY`: Only React/TS/WebGL files changed (~10s UI rebuild).
   - `ENGINE_INCREMENTAL`: `engine/` C++ or patches changed (~2–10m incremental `autoninja` build on your Linux server + upload new `ArdiaCore-<short>-linux-x64.tar.xz`).
   - `ENGINE_FULL`: `CHROMIUM_VERSION` or `HELIUM_TAG` bumped in `engine/patches/build/env.sh` (~2–4h full build on your Linux server).

2. **Rebase onto upstream and build the WebGL UI bundle**:
   ```bash
   ./scripts/linux/sync-upstream.sh
   ```

---

## 6. Build Acceleration with `sccache` (Local VM + Upstream CI Jobs)

To keep both local Linux engine builds (e.g. 16–18 vCPUs / 44–48 GB RAM on i9-10900KF) and upstream CI engine/app jobs fast:

1. **Local & Dedicated Engine Builder (`sccache` + optional R2/S3 backend)**:
   - Install `sccache` (`sudo pacman -S sccache`) and set:
     ```bash
     export SCCACHE_DIR="$HOME/.cache/sccache"
     export SCCACHE_CACHE_SIZE="50G"
     ```
   - Or back it with Cloudflare R2 / S3 (`SCCACHE_BUCKET`, `SCCACHE_ENDPOINT`, `SCCACHE_REGION=auto`) so any Linux build runner or VM shares the same precompiled Chromium `.o` cache.
   - Pass `cc_wrapper="sccache"` in `args.gn` (`engine/patches/build/gn_defines.sh` when `command -v sccache` is present), plus `use_thin_lto=false` and `concurrent_links=1` on dev/porting builds to avoid linker RAM spikes.
2. **Upstream GitHub Actions CI (`.github/workflows/build.yml`)**:
   - **Per-commit `app-linux-x64` job (`ubuntu-latest`, ~3 min)**: Uses `actions/cache@v6` on `apps/browser/build-ardiacore/prebuilt-linux-x86_64` with `packages/ardiacore/scripts/fetch-engine.sh --arch linux-x86_64 --nearest`, builds `apps/demo` (`pnpm --filter @ardia/demo build`), and packages the Linux release tarball without compiling Chromium on GitHub's runners.
   - **Engine release workflow (if building `engine/` in CI or self-hosted runner)**: Wire `mozilla-actions/sccache-action@v0.0.9` (with `SCCACHE_GHA_ENABLED=true` or R2 bucket credentials) so incremental or clean engine rebuilds hit cached object files across runs.
