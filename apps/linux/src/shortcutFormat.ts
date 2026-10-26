import { formatShortcut as sharedFormat, type Shortcut } from "../../browser/src/state/commands/shortcuts";

export * from "../../browser/src/state/commands/shortcuts";

const modifierNames = { command: "Ctrl", control: "Super", option: "Alt", shift: "Shift", function: "Fn" } as const;
const modifierOrder = ["command", "control", "option", "shift", "function"] as const;
const keyNames: Record<string, string> = {
  "\t": "Tab", "\r": "Enter", "\u0003": "NumpadEnter", "\u0008": "Backspace", "\u007f": "Backspace",
  "": "Delete", "\u001b": "Esc", "": "Home", "": "End", "": "PageUp", "": "PageDown",
};

export function formatShortcut({ key, modifiers }: Shortcut): string {
  if (!key) return "";
  const names = modifierOrder.filter((modifier) => modifiers.includes(modifier)).map((modifier) => modifierNames[modifier]);
  return [...names, keyNames[key] ?? sharedFormat({ key, modifiers: [] })].join("+");
}
