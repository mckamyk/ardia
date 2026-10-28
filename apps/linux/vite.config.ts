import { fileURLToPath } from "node:url";
import type { Plugin } from "vite";
import { webShellConfig } from "./vite.shared";
import { inputAdapters, rawBookmarkAdapter, resolveInputAdapter } from "./inputAdapters";
import { credentialWording } from "./credentialWording";

const here = (path: string) => fileURLToPath(new URL(path, import.meta.url));
const replacements = new Map([
  ...inputAdapters,
  [here("../browser/src/platform/livePersistence.ts"), here("./src/livePersistence.ts")],
  [here("../../packages/core/src/suggestFetcher.ts"), here("./src/suggestFetcher.ts")],
  [here("../browser/src/components/settings/settingsIndex.ts"), here("./src/settingsIndex.ts")],
  [here("../browser/src/components/settings/Form.tsx"), here("./src/settingsForm.tsx")],
  [here("../browser/src/state/engines/engines.ts"), here("./src/hostEngines.ts")],
  [here("../browser/src/components/sidebar/HoverCard.tsx"), here("./src/SidebarHoverCard.tsx")],
  [here("../browser/src/state/settings/settingsWindows.ts"), here("./src/settingsWindows.ts")],
  [here("../browser/src/components/profiles/ProfileIndicator.tsx"), here("./src/ProfileIndicator.tsx")],
  [here("../browser/src/components/sidebar/AddressBar.tsx"), here("./src/SidebarAddressBar.tsx")],
  [here("../browser/src/components/sidebar/TileFace.tsx"), here("./src/TileFace.tsx")],
  [here("../browser/src/components/site/SiteConnection.tsx"), here("./src/SiteConnection.tsx")],
  [here("../browser/src/state/settings/paneData.ts"), here("./src/credentialPaneData.ts")],
  [here("../browser/src/components/settings/panes/Passwords.tsx"), here("./src/LinuxPasswordsPane.tsx")],
  [here("../browser/src/components/settings/panes/Autofill.tsx"), here("./src/LinuxAutofillPane.tsx")],
  [here("../browser/src/state/import/apply.ts"), here("./src/importApply.ts")],
  [here("../browser/src/state/tabs/tabKeepAlive.ts"), here("./src/tabKeepAlive.ts")],
  [here("../browser/src/state/sync/engine.ts"), here("./src/syncEngine.ts")],
  [here("../browser/src/state/extensions/store.ts"), here("./src/extensionsStore.ts")],
]);
const importedPasswordSave = {
  source: here("../browser/src/state/site/passwords.ts"),
  importer: here("../browser/src/components/settings/panes/Passwords.tsx"),
  replacement: here("./src/importedPasswordSave.ts"),
};
const tabLifecycle = here("../browser/src/state/tabs/tabLifecycle.ts");
const importerReplacements = new Map([
  [`${here("../browser/src/state/splits/actions.ts")}\0${tabLifecycle}`, here("./src/policyToasts.ts")],
  [`${here("../browser/src/platform/performance.ts")}\0${tabLifecycle}`, here("./src/policyPerformance.ts")],
]);
const adapters = new Set([...replacements.values(), ...importerReplacements.values(), rawBookmarkAdapter.replacement, importedPasswordSave.replacement]);

function linuxComponents(): Plugin {
  return {
    name: "ardia-linux:components",
    enforce: "pre",
    transform(code, id) {
      if (!id.includes("/apps/browser/src/components/")) return null;
      if (id.endsWith("/settings/searchMatch.ts") || !/[⌘⌥⇧⌃]/.test(code)) return null;
      return { code: code.replaceAll("⌘", "Ctrl+").replaceAll("⌥", "Alt+").replaceAll("⇧", "Shift+").replaceAll("⌃", "Super+"), map: null };
    },
    async resolveId(source, importer, options) {
      if (!importer || adapters.has(importer)) return null;
      const resolved = await this.resolve(source, importer, { ...options, skipSelf: true });
      if (resolved?.id === importedPasswordSave.source && importer === importedPasswordSave.importer) return importedPasswordSave.replacement;
      const scoped = resolved && importerReplacements.get(`${resolved.id}\0${importer}`);
      if (scoped) return scoped;
      return resolved ? resolveInputAdapter(resolved.id, importer) ?? replacements.get(resolved.id) ?? null : null;
    },
  };
}

export default webShellConfig({
  plugins: [linuxComponents(), credentialWording()],
  aliases: [
    { find: /^@ardia\/shell$/, replacement: here("./src/shims/shell.tsx") },
    { find: /^@ardia\/ardiacore$/, replacement: here("./src/shims/ardiacore.tsx") },
  ],
});
