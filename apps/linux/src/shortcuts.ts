import type { BrowserCommand, MenuShortcut, ShortcutModifier } from "@ardia-real/shell";

export type Mod = "cmd" | "shift" | "alt" | "ctrl";
export type Shortcut = { code: string; mods: Mod[]; command: BrowserCommand; arg?: string | (() => string | null); notInText?: boolean };
export type Binding = { keyCode: number; modifiers: number; command: BrowserCommand; arg: string | null; notInText: boolean };

const s = (code: string, mods: Mod[], command: BrowserCommand, extra: Partial<Shortcut> = {}): Shortcut => ({ code, mods, command, ...extra });
const digits = Array.from({ length: 9 }, (_, i) => i + 1);

export const SHORTCUTS: Shortcut[] = [
  s("Comma", ["cmd"], "openSettings"),
  s("KeyT", ["cmd"], "newTab"),
  s("KeyT", ["cmd", "alt"], "newTabInGroup"),
  s("KeyT", ["cmd", "shift"], "reopenClosedTab"),
  s("KeyN", ["cmd"], "newWindow"),
  s("KeyN", ["cmd", "shift"], "newIncognitoWindow"),
  s("KeyN", ["cmd", "alt"], "newLittleArdia"),
  s("KeyN", ["cmd", "ctrl"], "newGroupWithTabs"),
  s("KeyO", ["cmd"], "openFile"),
  s("KeyL", ["cmd"], "focusCommandBar"),
  s("F6", [], "focusCommandBar"),
  s("KeyD", ["alt"], "focusCommandBar"),
  s("KeyW", ["cmd"], "closeTab"),
  s("KeyW", ["cmd", "ctrl"], "closeTabGroup"),
  s("KeyK", ["cmd", "shift"], "closeAllTabs"),
  s("KeyK", ["cmd", "alt"], "cleanUpTabs"),
  s("KeyI", ["cmd", "shift"], "devTools"),
  s("KeyP", ["cmd"], "print"),
  s("KeyP", ["cmd", "alt"], "printWithSystemDialog"),
  s("KeyP", ["cmd", "ctrl"], "newGroupWithTabs"),
  s("KeyC", ["cmd", "shift"], "copyUrl"),
  s("KeyC", ["cmd", "alt", "shift"], "copyUrlAsMarkdown"),
  s("KeyC", ["cmd", "ctrl"], "newTabInGroup"),
  s("KeyF", ["cmd"], "findInPage"),
  s("KeyF", ["cmd", "alt"], "findAndReplace"),
  s("KeyG", ["cmd"], "findNext"),
  s("KeyG", ["cmd", "shift"], "findPrevious"),
  s("KeyE", ["cmd"], "useSelectionForFind"),
  s("", [], "jumpToSelection"),
  s("Period", ["cmd"], "stop"),
  s("KeyR", ["cmd"], "reload"),
  s("F5", [], "reload"),
  s("KeyR", ["cmd", "shift"], "forceReload"),
  s("F5", ["shift"], "forceReload"),
  s("KeyS", ["cmd", "shift"], "toggleTabLayout"),
  s("KeyS", ["cmd"], "toggleSidebar"),
  s("KeyL", ["cmd", "shift"], "toggleSidebar"),
  s("KeyF", ["cmd", "shift"], "toggleSidebar"),
  s("Equal", ["ctrl", "shift"], "openSplitPane"),
  s("BracketRight", ["ctrl", "shift"], "focusNextPane"),
  s("BracketLeft", ["ctrl", "shift"], "focusPreviousPane"),
  s("KeyB", ["cmd", "shift"], "toggleBookmarksBar"),
  s("Digit0", ["cmd"], "zoomReset"),
  s("Equal", ["cmd"], "zoomIn"),
  s("Equal", ["cmd", "shift"], "zoomIn"),
  s("Minus", ["cmd"], "zoomOut"),
  s("KeyU", ["cmd", "alt"], "viewSource"),
  s("KeyI", ["cmd", "alt"], "devTools"),
  s("KeyC", ["cmd", "alt"], "inspectElements"),
  s("KeyJ", ["cmd", "alt"], "javaScriptConsole"),
  s("F12", [], "toggleDevTools"),
  s("F7", [], "caretBrowsing"),
  ...digits.slice(0, 8).map((n) => s(`Digit${n}`, ["cmd"], "selectTab", { arg: `${n}` })),
  s("Digit9", ["cmd"], "selectLastTab"),
  ...digits.map((n) => s(`Digit${n}`, ["ctrl"], "switchProfile", { arg: () => `${n}` })),
  s("BracketLeft", ["cmd"], "back"),
  s("BracketRight", ["cmd"], "forward"),
  s("ArrowLeft", ["alt"], "back"),
  s("ArrowRight", ["alt"], "forward"),
  s("BracketRight", ["cmd", "shift"], "nextTab"),
  s("BracketLeft", ["cmd", "shift"], "previousTab"),
  s("ArrowRight", ["cmd", "alt"], "nextTab"),
  s("ArrowLeft", ["cmd", "alt"], "previousTab"),
  s("PageDown", ["cmd"], "nextTab"),
  s("PageUp", ["cmd"], "previousTab"),
  s("ArrowDown", ["cmd", "alt"], "nextTab"),
  s("ArrowUp", ["cmd", "alt"], "previousTab"),
  s("PageDown", ["ctrl", "shift"], "moveTabDown"),
  s("PageUp", ["ctrl", "shift"], "moveTabUp"),
  s("KeyA", ["cmd", "shift"], "searchTabs"),
  s("Tab", ["cmd"], "tabSwitcher", { arg: "forward",  }),
  s("Tab", ["cmd", "shift"], "tabSwitcher", { arg: "backward",  }),
  s("Enter", ["cmd"], "returnToPinnedUrl", { notInText: true }),
  s("KeyD", ["cmd"], "bookmarkPage"),
  s("KeyD", ["cmd", "shift"], "bookmarkAllTabs"),
  s("KeyB", ["cmd", "alt"], "manageBookmarks"),
  s("KeyY", ["cmd"], "showHistory"),
  s("Backspace", ["cmd", "shift"], "clearBrowsingData"),
  s("KeyJ", ["cmd", "shift"], "javaScriptConsole"),
  s("KeyJ", ["cmd"], "downloads"),
  s("KeyL", ["cmd", "alt"], "downloads"),
  s("KeyM", ["cmd", "shift"], "openProfileMenu"),
];

const modifierNames: Record<Mod, ShortcutModifier> = { cmd: "command", ctrl: "control", alt: "option", shift: "shift" };
const modifierFlags: Record<ShortcutModifier, number> = { command: 4, control: 16, option: 8, shift: 2, function: 0 };
const specialKeys: Record<string, [string, number]> = {
  Comma: [",", 188], Period: [".", 190], Equal: ["=", 187], Minus: ["-", 189],
  BracketLeft: ["[", 219], BracketRight: ["]", 221], Tab: ["\t", 9], Enter: ["\r", 13],
  Backspace: ["\u0008", 8], Delete: ["", 46], Escape: ["\u001b", 27], Space: [" ", 32],
  ArrowLeft: ["", 37], ArrowRight: ["", 39], ArrowUp: ["", 38], ArrowDown: ["", 40],
  PageUp: ["", 33], PageDown: ["", 34], Home: ["", 36], End: ["", 35],
};
const keyOf = (code: string): [string, number] | null => {
  if (/^Key[A-Z]$/.test(code)) return [code.slice(3).toLowerCase(), code.charCodeAt(3)];
  if (/^Digit[0-9]$/.test(code)) return [code.slice(5), code.charCodeAt(5)];
  if (/^F([1-9]|1[0-9]|20)$/.test(code)) return [String.fromCharCode(0xf703 + Number(code.slice(1))), 111 + Number(code.slice(1))];
  return specialKeys[code] ?? null;
};
const codeOf = (key: string): number | null => {
  if (/^[a-z0-9]$/i.test(key)) return key.toUpperCase().charCodeAt(0);
  const point = key.charCodeAt(0);
  if (key.length === 1 && point >= 0xf704 && point <= 0xf717) return 112 + point - 0xf704;
  return Object.values(specialKeys).find(([value]) => value === key)?.[1] ?? null;
};
const shortcutId = (shortcut: Shortcut) => shortcut.command + (typeof shortcut.arg === "string" ? `:${shortcut.arg}` : "");
const comboId = (key: string, modifiers: string[]) => `${key.toLowerCase()}:${[...modifiers].sort().join(",")}`;
const defaultsOf = (shortcut: Shortcut) => ({ key: keyOf(shortcut.code)?.[0] ?? "", modifiers: shortcut.mods.map((mod) => modifierNames[mod]) });
const commandTitles: Partial<Record<BrowserCommand, string>> = {
  focusCommandBar: "Focus Address Bar", newTab: "New Tab", reopenClosedTab: "Reopen Closed Tab", openSettings: "Settings…",
  tabSwitcher: "Switch Tabs", zoomReset: "Actual Size", zoomIn: "Zoom In", zoomOut: "Zoom Out", devTools: "Developer Tools",
  toggleDevTools: "Toggle Developer Tools", javaScriptConsole: "JavaScript Console", copyUrl: "Copy URL", copyUrlAsMarkdown: "Copy URL as Markdown",
};
const titleOf = (shortcut: Shortcut) => shortcut.command === "tabSwitcher" ? (shortcut.arg === "backward" ? "Switch to Previous Tab" : "Switch to Next Tab") : (commandTitles[shortcut.command] ?? shortcut.command.replace(/([A-Z])/g, " $1").replace(/^./, (c) => c.toUpperCase())) + (typeof shortcut.arg === "string" && /^\d$/.test(shortcut.arg) ? ` ${shortcut.arg}` : "");
const pathOf = (command: string) => /Settings|Profile/.test(command) ? "Ardia" : /Tab|Window|Group/.test(command) ? "Tabs" : /find|Find|Selection|copy/.test(command) ? "Edit" : /bookmark|Bookmark/.test(command) ? "Bookmarks" : /History|Browsing|downloads|back|forward/.test(command) ? "History" : "View";

export function linuxMenuShortcuts(overrides: Record<string, string[]> = {}): MenuShortcut[] {
  const items = new Map<string, MenuShortcut>();
  for (const shortcut of SHORTCUTS) {
    const id = shortcutId(shortcut);
    const combo = defaultsOf(shortcut);
    const previous = items.get(id);
    if (previous) {
      previous.aliases!.push([combo.key, ...combo.modifiers]);
      continue;
    }
    const override = overrides[id];
    items.set(id, { id, title: titleOf(shortcut), path: [pathOf(shortcut.command)], key: override?.[0] ?? combo.key,
      modifiers: override ? override.slice(1) as ShortcutModifier[] : combo.modifiers,
      defaultKey: combo.key, defaultModifiers: combo.modifiers, remappable: true, aliases: [] });
  }
  return [...items.values()];
}

export function linuxBindings(overrides: Record<string, string[]> = {}, profiles: string[] = []): Binding[] {
  const liveIds = new Set(SHORTCUTS.map(shortcutId));
  const taken = new Set(Object.entries(overrides).filter(([id, value]) => liveIds.has(id) && value[0]).map(([, value]) => comboId(value[0]!, value.slice(1))));
  const bound = new Set<string>();
  return SHORTCUTS.flatMap((shortcut) => {
    const override = overrides[shortcutId(shortcut)];
    const combo = override ? { key: override[0] ?? "", modifiers: override.slice(1) } : defaultsOf(shortcut);
    const id = comboId(combo.key, combo.modifiers);
    if (!combo.key || bound.has(id) || !override && taken.has(id)) return [];
    const keyCode = codeOf(combo.key);
    if (!keyCode || combo.modifiers.some((m) => !(m in modifierFlags))) return [];
    const arg = typeof shortcut.arg === "function" ? shortcut.arg() : shortcut.arg ?? null;
    if (shortcut.command === "switchProfile" && (!arg || !profiles[Number(arg) - 1])) return [];
    bound.add(id);
    return [{ keyCode, modifiers: combo.modifiers.reduce((flags, mod) => flags | modifierFlags[mod as ShortcutModifier], 0), command: shortcut.command,
      arg: shortcut.command === "switchProfile" ? profiles[Number(arg) - 1]! : arg, notInText: shortcut.notInText ?? false }];
  });
}

export function keyboardCombo(event: Pick<KeyboardEvent, "code" | "key" | "ctrlKey" | "metaKey" | "altKey" | "shiftKey">) {
  const key = keyOf(event.code)?.[0] ?? (event.key.length === 1 ? event.key.toLowerCase() : "");
  const modifiers: ShortcutModifier[] = [];
  if (event.ctrlKey) modifiers.push("command");
  if (event.metaKey) modifiers.push("control");
  if (event.altKey) modifiers.push("option");
  if (event.shiftKey) modifiers.push("shift");
  return { key, modifiers, keyCode: codeOf(key), flags: modifiers.reduce((flags, mod) => flags | modifierFlags[mod], 0) };
}

export function linuxNativeBindings(bindings: Binding[], profiles: string[] = []) {
  const bound = new Set(bindings.map((binding) => `${binding.keyCode}:${binding.modifiers}`));
  return [...bindings, ...linuxBindings({}, profiles).filter((binding) => !bound.has(`${binding.keyCode}:${binding.modifiers}`)).map((binding) => ({ ...binding, suppressed: true }))];
}
