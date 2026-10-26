import { fileURLToPath } from "node:url";

const here = (path: string) => fileURLToPath(new URL(path, import.meta.url));
export const inputAdapters = new Map([
  [here("../browser/src/state/commands/commandShortcuts.ts"), here("./src/shortcutState.ts")],
  [here("../browser/src/state/commands/commandCatalog.ts"), here("./src/commandCatalog.ts")],
  [here("../browser/src/state/commands/shortcuts.ts"), here("./src/shortcutFormat.ts")],
  [here("../browser/src/platform/commands.ts"), here("./src/shortcutCommands.ts")],
  [here("../browser/src/lib/keys.ts"), here("./src/clickModifiers.ts")],
  [here("../browser/src/state/bookmarks/actions.ts"), here("./src/bookmarkActions.ts")],
]);
