import assert from "node:assert/strict";
import { test } from "node:test";
import { keyboardCombo, linuxBindings, linuxMenuShortcuts, linuxNativeBindings } from "./shortcuts.ts";

const commandAt = (bindings, keyCode, modifiers) => bindings.find((binding) => binding.keyCode === keyCode && binding.modifiers === modifiers);
const event = (code, modifiers = {}) => ({ code, key: "", ctrlKey: false, metaKey: false, altKey: false, shiftKey: false, ...modifiers });

test("Settings exposes the Linux bindings with their defaults and aliases", () => {
  const items = linuxMenuShortcuts();
  assert(items.length > 60);
  assert.equal(items.find((item) => item.id === "newTab").defaultKey, "t");
  assert.deepEqual(items.find((item) => item.id === "focusCommandBar").defaultModifiers, ["command"]);
  assert(items.find((item) => item.id === "toggleSidebar").aliases.length > 0);
  assert.equal(new Set(items.map((item) => item.id)).size, items.length);
});

test("physical Linux Control reaches the native new-tab and address-bar commands", () => {
  const bindings = linuxBindings();
  for (const [code, command] of [["KeyT", "newTab"], ["KeyL", "focusCommandBar"], ["Comma", "openSettings"]]) {
    const combo = keyboardCombo(event(code, { ctrlKey: true }));
    assert.equal(commandAt(bindings, combo.keyCode, combo.flags)?.command, command);
  }
  assert.equal(keyboardCombo(event("KeyL", { metaKey: true })).flags, 16);
});

test("Ctrl+Tab and Ctrl+Shift+Tab retain their direction", () => {
  const bindings = linuxBindings();
  assert.equal(commandAt(bindings, 9, 4)?.arg, "forward");
  assert.equal(commandAt(bindings, 9, 6)?.arg, "backward");
  assert.equal(commandAt(bindings, 34, 4)?.command, "nextTab");
});

test("a custom binding takes priority over another command's defaults", () => {
  const bindings = linuxBindings({ focusCommandBar: ["t", "command"] });
  assert.equal(commandAt(bindings, 84, 4)?.command, "focusCommandBar");
  assert(!commandAt(bindings, 76, 4));
  assert.equal(bindings.filter((binding) => binding.keyCode === 84 && binding.modifiers === 4).length, 1);
});

test("remapping and disabling remove every alias, and resetting restores them", () => {
  const bindings = linuxBindings({ toggleSidebar: ["q", "command", "shift"], newTab: [""] });
  assert.equal(commandAt(bindings, 81, 6)?.command, "toggleSidebar");
  assert(!commandAt(bindings, 83, 4));
  assert(!commandAt(bindings, 76, 6));
  assert(!commandAt(bindings, 84, 4));
  assert.equal(commandAt(linuxBindings(), 83, 4)?.command, "toggleSidebar");
});

test("numbered profile commands use native profile identities and skip missing profiles", () => {
  const bindings = linuxBindings({}, ["work", "personal"]);
  assert.equal(commandAt(bindings, 49, 16)?.arg, "work");
  assert.equal(commandAt(bindings, 50, 16)?.arg, "personal");
  assert(!commandAt(bindings, 51, 16));
});

test("function-key bindings record and dispatch without modifiers", () => {
  const combo = keyboardCombo(event("F6"));
  const binding = commandAt(linuxBindings({ focusCommandBar: [combo.key] }), combo.keyCode, combo.flags);
  assert.equal(binding?.command, "focusCommandBar");
  assert.equal(binding?.keyCode, 117);
});

test("obsolete overrides do not steal current default bindings", () => {
  assert.equal(commandAt(linuxBindings({ removedCommand: ["t", "command"] }), 84, 4)?.command, "newTab");
});

test("displaced defaults remain registered to consume Chromium's accelerators", () => {
  const active = linuxBindings({ newTab: [""], focusCommandBar: ["q", "command"] });
  const native = linuxNativeBindings(active);
  assert.equal(commandAt(native, 84, 4)?.suppressed, true);
  assert.equal(commandAt(native, 76, 4)?.suppressed, true);
  assert.equal(commandAt(native, 81, 4)?.command, "focusCommandBar");
  assert(!commandAt(native, 81, 4)?.suppressed);
});

test("utility windows consume browser shortcuts they cannot perform", () => {
  const active = linuxBindings().filter((binding) => ["closeTab", "openSettings", "findInPage"].includes(binding.command));
  assert.equal(commandAt(linuxNativeBindings(active), 84, 4)?.suppressed, true);
});

test("Ctrl+J dispatches the advertised Downloads command", () => {
  assert.equal(commandAt(linuxBindings(), 74, 4)?.command, "downloads");
});
