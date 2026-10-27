import { fileURLToPath } from "node:url";
import type { Plugin } from "vite";
import { webShellConfig } from "./vite.shared";

const here = (path: string) => fileURLToPath(new URL(path, import.meta.url));
const replacements = new Map([
  [here("../browser/src/components/sidebar/HoverCard.tsx"), here("./src/SidebarHoverCard.tsx")],
  [here("../browser/src/state/settings/settingsWindows.ts"), here("./src/settingsWindows.ts")],
  [here("../browser/src/components/profiles/ProfileIndicator.tsx"), here("./src/ProfileIndicator.tsx")],
  [here("../browser/src/components/sidebar/AddressBar.tsx"), here("./src/SidebarAddressBar.tsx")],
  [here("../browser/src/components/sidebar/TileFace.tsx"), here("./src/TileFace.tsx")],
]);
const adapters = new Set(replacements.values());

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
      return resolved ? replacements.get(resolved.id) ?? null : null;
    },
  };
}

export default webShellConfig({
  plugins: [linuxComponents()],
  aliases: [
    { find: /^@ardia\/shell$/, replacement: here("./src/shims/shell.tsx") },
    { find: /^@ardia\/ardiacore$/, replacement: here("./src/shims/ardiacore.tsx") },
  ],
});
