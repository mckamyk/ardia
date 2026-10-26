import { setMenuBarShortcuts as setSharedMenuBarShortcuts } from "../../browser/src/state/commands/commandShortcuts";
import type { MenuShortcut } from "../../browser/src/platform/commands";
import { useBrowser } from "../../browser/src/state/browser/store";
import { linuxMenuShortcuts } from "./shortcuts";

export * from "../../browser/src/state/commands/commandShortcuts";

let listed = false;

const refresh = () => {
  const state = useBrowser.getState();
  setSharedMenuBarShortcuts(linuxMenuShortcuts(state.settings.shortcuts, state.profileOrder));
};

export function setMenuBarShortcuts(_items: readonly MenuShortcut[]) {
  listed = true;
  refresh();
}

useBrowser.subscribe((state, previous) => {
  if (listed && (state.profileOrder !== previous.profileOrder || state.settings.shortcuts !== previous.settings.shortcuts)) refresh();
});
