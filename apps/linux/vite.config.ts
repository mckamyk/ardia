import { fileURLToPath } from "node:url";
import type { Plugin } from "vite";
import { webShellConfig } from "../demo/vite.shared";

const here = (path: string) => fileURLToPath(new URL(path, import.meta.url));
const replacements = new Map([
  [here("../browser/src/components/settings/windows.ts"), here("./src/settingsWindows.ts")],
  [here("../browser/src/components/ProfileIndicator.tsx"), here("./src/ProfileIndicator.tsx")],
  [here("../browser/src/components/sidebar/AddressBar.tsx"), here("./src/SidebarAddressBar.tsx")],
]);
const adapters = new Set(replacements.values());

function linuxComponents(): Plugin {
  return {
    name: "ardia-linux:components",
    enforce: "pre",
    transform(code, id) {
      if (!id.includes("/apps/browser/src/components/") && !id.endsWith("/apps/demo/src/shims/web/menus.tsx")) return null;
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
