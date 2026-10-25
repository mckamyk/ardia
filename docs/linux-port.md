# Ardia on Linux (Omarchy / Arch / Wayland) — Architecture & Porting Guide

This document describes how `mckamyk/ardia` brings Ardia to Linux (specifically targeting **Omarchy / Arch Linux + Hyprland / Wayland**) while remaining a clean, low-maintenance, long-lived fork of [`mantrakp04/ardia`](https://github.com/mantrakp04/ardia).

---

## 1. Why ~90% of the Linux Port Already Exists Upstream

Although upstream Ardia targets macOS (`arm64`) using `react-native-macos`, `AppKit`, and `Metal`, the repository already contains almost every piece needed for a native Linux browser:

| Layer | Upstream Path | Linux Status | Why It Works on Linux |
|---|---|---|---|
| **Browser UI & State** | `apps/browser/src/`, `packages/core/` | **100% Ready** | Written in TypeScript, React 19, and Zustand (Spaces, profiles, 3-tier tabs, folders, `Cmd+T` / `Ctrl+T` command palette, Little Ardia, settings). |
| **`react-native-web` + WebGL Shims** | `apps/demo/` (`vite.config.ts`, `src/shims/`) | **95% Ready** | Built by upstream for the interactive `ardia.dev` web demo and enforced on every push to `main` via `pnpm typecheck`. Translates `react-native-macos` to `react-native-web`, Metal shaders (`packages/shaders`) to WebGL (`src/shims/web/gl/`), macOS SF Symbols to inline SVG (`src/shims/web/symbols/`), and AppKit primitives (`Surface`, `MouseArea`, `Pager`, `FadeLabel`, `ContextMenuArea`) to DOM. |
| **Engine C ABI** | `engine/chromium/src/chrome/browser/ardia/public/ac_engine.h` | **100% Ready** | The entire boundary between the UI host and Chromium is `AC_ENGINE_ABI_VERSION 2`: two plain C JSON functions (`ac_engine_call_t` and `ac_engine_set_event_sink_t`). It has only been modified twice in repo history. |
| **Chromium C++ Services** | `engine/chromium/src/chrome/browser/ardia/ac_*.cc` | **92% Ready** | 37 of the 40 source files (`ac_engine.cc`, `ac_tabs.cc`, `ac_cookies.cc`, `ac_omnibox.cc`, `ac_history.cc`, `ac_bookmarks.cc`, `ac_extensions.cc`, `ac_downloads.cc`, `ac_cli_control.cc`, etc.) are standard cross-platform Chromium C++. Only 3 small files are `.mm` (`ac_external_apps.mm`, `ac_input_frame_rate.mm`, `pip/ac_pip_mac.mm`). |
| **Helium Chromium Base** | `engine/patches/build/` | **Ready via [`imputnet/helium-linux`](https://github.com/imputnet/helium-linux)** | Upstream builds against `imputnet/helium-chromium` + `imputnet/helium-macos`. Helium already maintains `imputnet/helium-linux` for the exact same `helium-chromium` patch series with `use_vaapi=true` and `rtc_use_pipewire=true`. |
| **CDP Automation CLI** | `apps/cli/` | **100% Ready** | Python + `pydoll` over Chrome DevTools Protocol. |

---

## 2. The Missing ~10%: What Needs to Be Built

To run Ardia natively on Linux without `react-native-macos` or `remote_cocoa`, we use the **Vivaldi / WebUI-Shell architecture**: Chromium's own cross-platform `views::Widget` (Aura + Ozone/Wayland) renders the `apps/demo` (`react-native-web` + WebGL) bundle as the window chrome and embeds each tab's real `content::WebContents` in the page viewport.

### Step 1: Gate macOS-Only Files in `engine/chromium/src/chrome/browser/ardia/BUILD.gn` (~100 LOC)
1. Change `assert(is_mac)` at the top of `engine/chromium/src/chrome/browser/ardia/BUILD.gn` to `assert(is_mac || is_linux)`.
2. Move `ac_external_apps.mm`, `ac_input_frame_rate.mm`, and `pip/ac_pip_mac.mm` into an `if (is_mac)` block, and add thin Linux `.cc` implementations in an `if (is_linux)` block:
   - `ac_external_apps_linux.cc`: `platform_util::OpenExternal` (`xdg-open`).
   - `ac_input_frame_rate_linux.cc`: no-op or Wayland frame hint.
   - `pip/ac_pip_linux.cc`: standard Aura `PictureInPictureBrowserFrameView` bounds helper.

### Step 2: Create the Linux Window & Tab Host in `engine/ardiacore-linux/` (~1,500–2,000 LOC C++)
Instead of `engine/ardiacore/` (which uses `NSWindow`, `BridgedContentView`, and `WebContentsViewCocoa`), create `engine/ardiacore-linux/src/ardia/core/`:
1. **`ac_main_delegate_linux.cc`**: Starts `ChromeMain` and initializes the Aura/Ozone window host.
2. **`ac_browser_window_linux.cc`**: Implements `BrowserWindow` (headless of default Chromium top toolbar/tabstrip, matching `ac_browser_window.mm`) so each Space/Profile gets its own `Browser*` instance inside one top-level `views::Widget`.
3. **`ac_shell_view_linux.cc`**:
   - Hosts a background `views::WebView` loading the compiled `apps/demo` UI bundle (`chrome://ardia-ui/` or local asset pak).
   - Hosts a foreground child `views::WebView` positioned at the `{x, y, width, height}` viewport rect reported by the React UI, attached to the active tab's `content::WebContents`.
   - Exposes `ac_engine_call` and `ac_engine_set_event_sink` (`public/ac_engine.h`) to the UI `WebContents` via a lightweight Mojo / `WebUIMessageHandler` bridge (`window.__ardiaHost.call(profileDir, json)` and `window.__ardiaHost.onEvent`).

### Step 3: Wire `apps/demo/src/shims/` to the Live Engine When Running in Host Mode (~500 LOC TS)
Keep `apps/demo` working in plain browsers, and detect `window.__ardiaHost` when running inside the Linux shell:
1. **`apps/demo/src/shims/ardiacore.tsx`**:
   - When `window.__ardiaHost` is present, route `ArdiaCEF.engineCall(name, ...args)` to `window.__ardiaHost.call(...)` instead of returning `"${name} is not on the web"`.
   - Subscribe to `window.__ardiaHost.onEvent((topic, json) => emitNativeEvent(...))` so tab updates, favicons, downloads, omnibox suggestions, and keyboard shortcuts flow into the Zustand store (`useBrowser`).
2. **`apps/demo/src/shims/web/WebView.tsx`**:
   - When `window.__ardiaHost` is present, replace the demo `<iframe>` (`framePool.ts`) with a transparent `<div ref={viewportRef} />` watched by a `ResizeObserver` that calls `window.__ardiaHost.setTabViewport(tabId, profileId, rect)` so the C++ `views::WebView` renders the real Chromium tab in that exact rectangle.

### Step 4: Linux Engine Build Scripts (`engine/patches/build/`)
1. Pin [`imputnet/helium-linux`](https://github.com/imputnet/helium-linux) alongside `imputnet/helium-chromium` in `engine/patches/build/helium-pins.ini`.
2. In `engine/patches/build/01-sync.sh`, set `target_os = ['linux']` when `uname -s` is `Linux`.
3. In `engine/patches/build/gn_defines.sh`, when building on Linux:
   - Set `target_cpu="x64"` (or `"arm64"` on ARM Linux).
   - Add `imputnet/helium-linux/flags.linux.gn` flags: `use_vaapi=true`, `rtc_use_pipewire=true`, `use_sysroot=true`, `is_clang=true`.
   - Set `enable_cdm_host_verification=false` (see Section 3 on DRM below).
4. In `engine/patches/build/03-helium.py`, replace macOS `sips` icon resizing in `generate_icons()` with `rsvg-convert` / `magick` / Python Pillow on Linux, and apply `helium-linux` patches instead of `helium-macos`.
5. In `engine/patches/series`, skip macOS-only patches (`chromium-window-hosted.patch`, `chromium-angle-metal-shader-cache.patch`, `chromium-video-super-resolution.patch`, `chromium-apple-passwords-host-skipped.patch`) when `OS=linux`.

---

## 3. Sanctioned / Gated Features on Linux (DRM, Passkeys, 1Password, Codecs, Web Store)

Moving Ardia to Linux affects sanctioned/gated browser features in several important ways—and **Widevine DRM actually works better on Linux than on macOS Ardia**:

### A. Widevine DRM (Netflix, Spotify, Prime Video, Disney+, Max)
- **Why macOS Ardia struggles with Widevine**: On macOS and Windows, Google's Widevine CDM enforces **VMP (Verified Media Path / CDM Host Verification, `enable_cdm_host_verification=true`)**. The CDM checks for a cryptographic `.sig` file (`Chromium Framework.framework.sig`) signed by Google's Widevine licensing authority. Because custom/open-source builds don't have a commercial Google Widevine VMP signature, `docs/engine-build.md` notes: *"Widevine (DRM still needs Google's VMP signing)"*.
- **Why Linux wins here**: On **Linux**, Google's `libwidevinecdm.so` **does not enforce VMP host binary signature verification**! In upstream Chromium (`media/cdm/cdm_paths.gni`), `enable_cdm_host_verification` is only enabled on `is_mac || is_win`.
- **How to enable full DRM on Omarchy / Arch**:
  1. In `gn_defines.sh` on Linux, set:
     ```gn
     proprietary_codecs=true
     ffmpeg_branding="Chrome"
     enable_widevine=true
     enable_cdm_host_verification=false
     ```
  2. Install `chromium-widevine` from the AUR (`yay -S chromium-widevine`) or symlink `WidevineCdm` from `google-chrome` into the browser's user data directory (`~/.config/ardia/WidevineCdm`).
  3. Netflix, Spotify Web Player, Prime Video, Hulu, and Twitch will play DRM-protected streams out of the box (Widevine L3).

### B. 1Password, Passkeys (WebAuthn), and Keychain Encryption
- **1Password & Passkeys**:
  - On macOS, Apple gates the system passkey entitlement (`com.apple.developer.web-browser.public-key-credential`) behind an Apple Developer provisioning profile, and 1Password checks macOS bundle signatures.
  - On Linux (Omarchy), the **1Password browser extension** (installed directly from the Chrome Web Store) handles WebAuthn passkeys in-browser (`navigator.credentials.get` / `create`) and communicates with the Linux `1password` desktop app over Native Messaging.
  - Because Ardia keeps Helium's `scan-chrome-native-messaging-hosts.patch`, it automatically discovers `/etc/opt/chrome/native-messaging-hosts/com.1password.1password.json` and `~/.config/chromium/NativeMessagingHosts/`. Adding the Ardia Linux binary under **1Password -> Settings -> Browser -> Additional Trusted Browsers** unlocks full biometric/Polkit unlock and passkey autofill.
  - Hardware FIDO2 security keys (YubiKey, etc.) work out of the box on Linux via `libudev` (`/dev/hidraw*`) with no OS entitlement required.
- **Cookie & Password Storage Encryption (`OSCrypt`)**:
  - Instead of macOS Keychain (`"Ardia Safe Storage"`), Chromium on Linux automatically uses `libsecret` (`gnome-keyring` / `keepassxc` / `kwallet` over D-Bus Secret Service, standard on Omarchy).

### C. H.264 / HEVC Hardware Acceleration & Hyprland Screen Sharing
- **`use_vaapi=true`** enables GPU hardware video decoding/encoding via VA-API on Intel, AMD (`radeonsi`), and NVIDIA (`nvidia-vaapi-driver`) on Arch/Omarchy.
- **`rtc_use_pipewire=true`** enables native Wayland screen and window sharing on Hyprland via `xdg-desktop-portal-hyprland` and PipeWire.

### D. Chrome Web Store & Component Updater
- Ardia's `chromium-webstore-direct.patch`, `chromium-extension-updates.patch`, and `chromium-component-updater-security-only.patch` are pure cross-platform C++.
- Updating the platform query parameter in `chromium-webstore-direct.patch` from `os=mac` to `os=linux` preserves one-click Chrome Web Store installs, automatic extension updates, and Google CRLSet / Chrome Root Store security updates.

### E. What Doesn't Carry Over to Linux
- **Apple Pay**: Requires macOS Safari/WebKit PassKit hardware enclave; unavailable on Linux (standard Web Payments / Autofill works).
- **Apple VideoToolbox Super Resolution (`ac_video_upscaler.mm`) & Apple Translation (`TranslateModule.swift`)**: macOS-only frameworks; disabled on Linux.
- **iCloud Drive Sync (`packages/sync`)**: Upstream syncs encrypted blobs via `~/Library/Mobile Documents`. On Linux, point the sync directory (`ARDIA_SYNC_DIR`) to a local folder synced by **Syncthing**, Nextcloud, or rclone; the encryption itself (`SyncCrypto`) is standard AES-GCM/HKDF.

---

## 4. Continuous Upstream Sync & Rebuild Workflow

Because upstream (`mantrakp04/ardia`) pushes multiple commits daily and enforces `apps/demo` (`react-native-web` + WebGL) typechecking in CI, keeping this fork updated is straightforward:

1. **Check for upstream updates (runs in ~2 seconds, no Chromium checkout needed)**:
   ```bash
   ./scripts/linux/check-upstream.sh
   ```
   Reports how many commits `mantrakp04/ardia` is ahead and classifies the incoming changes into:
   - **`UI_ONLY`**: Only `apps/browser/src`, `apps/demo`, or `packages/` TS files changed -> rebuild takes **~10 seconds** (`pnpm --filter @ardia/demo build`).
   - **`ENGINE_INCREMENTAL`**: Files in `engine/chromium/` or `engine/patches/` changed, but `CHROMIUM_VERSION` in `engine/patches/build/env.sh` is unchanged -> incremental `autoninja` build takes **~2–10 minutes**.
   - **`ENGINE_FULL`**: `CHROMIUM_VERSION` or `HELIUM_TAG` in `engine/patches/build/env.sh` bumped -> requires `01-sync.sh` + `03-helium.py` + `04-series.sh` + `05-build.sh` on the build server (**~2–4 hours**).

2. **Sync with upstream and verify the WebGL/RN-Web UI build**:
   ```bash
   ./scripts/linux/sync-upstream.sh
   ```
   Fetches `mantrakp04/ardia`, rebases local Linux commits on top of `upstream/main`, runs `pnpm install --frozen-lockfile && pnpm typecheck`, and builds the production `react-native-web` + WebGL bundle in `apps/demo/dist`.
