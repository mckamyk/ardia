import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { resolveHostModule } from "./hostModules.ts";
import { linuxShellMethod } from "./shellMethods.ts";
import { requireNativeModule } from "./shims/expo-modules-core.ts";

const read = (path) => readFileSync(new URL(path, import.meta.url), "utf8");
const nativeHost = read("../../../engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc");
const bootstrap = nativeHost.slice(nativeHost.indexOf("base::DictValue ShellHostView::Bootstrap() const {"), nativeHost.indexOf("void ShellHostView::LayoutOverlay()"));
const advertised = [...bootstrap.matchAll(/\.Append\("([A-Za-z]+\.[A-Za-z]+)"\)/g)].map((match) => match[1]);

const SHARED_MODULES = [
  ["ArdiaShell", "../../../packages/shell/src/shellNative.ts", "ShellNative"],
  ["ArdiaApp", "../../../packages/shell/src/app.ts", "AppModule"],
  ["ArdiaSystem", "../../../packages/shell/src/system.tsx", "SystemNative"],
  ["ArdiaMagnifyArea", "../../../packages/shell/src/system.tsx", "MagnifyAreaNative"],
  ["ArdiaFileDrop", "../../../packages/shell/src/system.tsx", "FileDropNative"],
  ["ArdiaCalendar", "../../../packages/shell/src/live.ts", "CalendarNative"],
  ["ArdiaKeychain", "../../../packages/shell/src/live.ts", "KeychainNative"],
  ["ArdiaDockSelection", "../../../packages/shell/src/views.tsx", "DockSelectionNative"],
  ["ArdiaInlineCompletion", "../../../packages/shell/src/inlineCompletion.ts", "InlineCompletionNative"],
  ["ArdiaTranslate", "../../../packages/shell/src/translation.ts", "TranslationModule"],
];

function declared(file, type) {
  const source = read(file);
  const start = source.indexOf(`export type ${type} = {`);
  assert.ok(start >= 0, `${file} no longer declares ${type}`);
  const open = source.indexOf("{", start);
  let depth = 0;
  let end = open;
  for (; end < source.length; end++) {
    if (source[end] === "{") depth++;
    if (source[end] === "}" && --depth === 0) break;
  }
  return [...source.slice(open + 1, end).matchAll(/^ {2}(\w+)\??(\(|<|:)/gm)]
    .map((match) => ({ name: match[1], method: match[2] !== ":" }))
    .filter((member) => member.name !== "addListener");
}

const linuxHost = () => ({
  bootstrap: { windowId: "main", profileId: "default", capabilities: [...advertised] },
  invoke: async (method) => { throw new Error(`Unsupported Linux host method: ${method}`); },
  call: async () => "null",
  onEvent: () => () => {},
});

const SHIM_SOURCES = ["./shims/shell.tsx", "./shims/shellBase.tsx", "./shortcutsRuntime.ts"].map(read).join("\n");
const fromShim = (method) => new RegExp(`export (?:const|function|async function) ${method}\\b|export \\{[^}]*\\b${method}\\b[^}]*\\}|hostShellLocals[^\\n]*\\b${method}\\b|method === "${method}"`).test(SHIM_SOURCES);
const nativelyImplemented = (method) => nativeHost.includes(`method == "ArdiaShell.${method}"`);

test("every method the shared native module types declare resolves to a callable on Linux, so a shared call can't throw 'is not a function'", () => {
  const host = linuxHost();
  const missing = [];
  for (const [module, file, type] of SHARED_MODULES) {
    for (const { name, method } of declared(file, type)) {
      if (method && typeof resolveHostModule(host, module, name) !== "function") missing.push(`${module}.${name}`);
    }
  }
  assert.deepEqual(missing, []);
});

test("every ArdiaShell method is implemented by the native host, a Linux shell method or a shell shim export", () => {
  const unimplemented = declared(SHARED_MODULES[0][1], SHARED_MODULES[0][2])
    .map(({ name }) => name)
    .filter((name) => !nativelyImplemented(name) && !linuxShellMethod(name) && !fromShim(name));
  assert.deepEqual(unimplemented, []);
  for (const name of ["watchLeftEdge", "setDragEscapeCapture", "saveKeyResponder", "raiseView", "animateWindowClose", "dragPreviewBegin"]) {
    assert.equal(resolveHostModule(linuxHost(), "ArdiaShell", name), linuxShellMethod(name), name);
  }
});

test("a method the native host advertises wins over the Linux fallback", async () => {
  const host = linuxHost();
  const calls = [];
  host.bootstrap.capabilities.push("ArdiaShell.watchLeftEdge");
  host.invoke = async (method, args) => { calls.push([method, args]); return null; };
  await resolveHostModule(host, "ArdiaShell", "watchLeftEdge")("main", true, 0);
  assert.deepEqual(calls, [["ArdiaShell.watchLeftEdge", { args: ["main", true, 0] }]]);
});

function fakeDom() {
  const listeners = new Map();
  const classes = new Set();
  const head = [];
  class HTMLElement {
    constructor(name, parent = null) {
      this.name = name;
      this.parentElement = parent;
      this.children = [];
      this.style = { zIndex: "" };
      this.isConnected = true;
      parent?.children.push(this);
    }
    focus() { dom.document.activeElement = this; }
    getBoundingClientRect() { return { left: 0, top: 0, right: 1920, bottom: 1080 }; }
  }
  const body = new HTMLElement("body");
  const frame = new HTMLElement("window", body);
  const dom = {
    HTMLElement,
    body,
    frame,
    listeners,
    window: {
      addEventListener: (type, listener) => listeners.set(type, [...(listeners.get(type) ?? []), listener]),
      removeEventListener: (type, listener) => listeners.set(type, (listeners.get(type) ?? []).filter((l) => l !== listener)),
    },
    document: {
      body,
      activeElement: body,
      fullscreenElement: null,
      focused: true,
      hasFocus: () => dom.document.focused,
      querySelector: (selector) => (selector === ".ardia-window" ? frame : null),
      getElementById: (id) => head.find((style) => style.id === id) ?? null,
      createElement: () => ({ id: "", textContent: "" }),
      head: { append: (style) => head.push(style) },
      documentElement: { classList: { toggle: (name, on) => (on ? classes.add(name) : classes.delete(name)) } },
      addEventListener: (type, listener) => listeners.set(type, [...(listeners.get(type) ?? []), listener]),
    },
    getComputedStyle: (element) => ({ zIndex: element.style.zIndex || "auto" }),
    fire: (type, event) => { for (const listener of listeners.get(type) ?? []) listener(event); },
  };
  return dom;
}

function installDom() {
  const dom = fakeDom();
  Object.assign(globalThis, { window: dom.window, document: dom.document, HTMLElement: dom.HTMLElement, getComputedStyle: dom.getComputedStyle });
  return dom;
}

function shellEvents(event) {
  const seen = [];
  const subscription = requireNativeModule("ArdiaShell").addListener(event, (payload) => seen.push(payload));
  return { seen, remove: () => subscription.remove() };
}

test("hiding the sidebar watches the left edge without a native watcher, and a peek's hold keeps it over while the pointer is in the hold", async () => {
  const dom = installDom();
  const events = shellEvents("onWindowEvent");
  const watch = resolveHostModule(linuxHost(), "ArdiaShell", "watchLeftEdge");
  const move = (clientX) => dom.fire("pointermove", { clientX, clientY: 300, shiftKey: false, ctrlKey: false, altKey: false, metaKey: false });
  await watch("main", true, 0);
  assert.deepEqual(events.seen.at(-1), { type: "leftEdge", id: "main", over: false, point: [-1, -1] });
  move(4);
  assert.equal(events.seen.at(-1).over, false);
  await watch("main", true, 230);
  move(4);
  assert.deepEqual(events.seen.at(-1), { type: "leftEdge", id: "main", over: true, point: [4, 300] });
  move(600);
  assert.deepEqual(events.seen.at(-1), { type: "leftEdge", id: "main", over: false, point: [600, 300] });
  await watch("main", false);
  const reported = events.seen.length;
  move(4);
  assert.equal(events.seen.length, reported);
  events.remove();
});

test("a tab drag captures a bare Escape as dragEscape and releases it when the drag ends", async () => {
  const dom = installDom();
  const events = shellEvents("onAppEvent");
  const capture = resolveHostModule(linuxHost(), "ArdiaShell", "setDragEscapeCapture");
  await capture(true);
  const escape = (extra = {}) => {
    const event = { key: "Escape", ctrlKey: false, metaKey: false, altKey: false, shiftKey: false, prevented: false, stopped: false, ...extra };
    event.preventDefault = () => { event.prevented = true; };
    event.stopImmediatePropagation = () => { event.stopped = true; };
    dom.fire("keydown", event);
    return event;
  };
  const bare = escape();
  assert.deepEqual(events.seen, [{ type: "dragEscape" }]);
  assert.ok(bare.prevented && bare.stopped);
  assert.equal(escape({ shiftKey: true }).prevented, false);
  await capture(true);
  escape();
  assert.equal(events.seen.length, 2);
  await capture(false);
  escape();
  assert.equal(events.seen.length, 2);
  assert.deepEqual(dom.listeners.get("keydown"), []);
  events.remove();
});

test("the key responder is saved and restored in the shell, and a page-focused window hands focus back to the page", async () => {
  const dom = installDom();
  const host = linuxHost();
  const [save, restore, discard] = ["saveKeyResponder", "restoreKeyResponder", "discardKeyResponder"].map((name) => resolveHostModule(host, "ArdiaShell", name));
  const field = new dom.HTMLElement("field", dom.frame);
  field.focus();
  const token = await save("main");
  assert.ok(token > 0);
  dom.body.focus();
  assert.equal(await restore(token), true);
  assert.equal(dom.document.activeElement, field);
  assert.equal(await restore(token), false);
  const again = await save("main");
  await discard(again);
  assert.equal(await restore(again), false);
  const gone = await save("main");
  field.isConnected = false;
  assert.equal(await restore(gone), false);
  dom.body.focus();
  assert.equal(await save("main"), 0);
  field.isConnected = true;
  field.focus();
  dom.document.focused = false;
  assert.equal(await save("main"), 0);
});

test("raiseView lifts a dragged chip and its parents above their siblings and puts them back", async () => {
  const dom = installDom();
  const raise = resolveHostModule(linuxHost(), "ArdiaShell", "raiseView");
  const strip = new dom.HTMLElement("strip", dom.frame);
  const group = new dom.HTMLElement("group", strip);
  const neighbour = new dom.HTMLElement("neighbour", strip);
  neighbour.style.zIndex = "3";
  const chip = new dom.HTMLElement("chip", group);
  new dom.HTMLElement("other", group);
  await raise(chip, 1, true);
  assert.equal(chip.style.zIndex, "1");
  assert.equal(group.style.zIndex, "4");
  assert.equal(strip.style.zIndex, "");
  await raise(chip, 1, false);
  assert.equal(chip.style.zIndex, "");
  assert.equal(group.style.zIndex, "");
  await raise(null, 0, true);
});

test("window close animations, the drag preview and focus cancellation settle immediately, and unavailable features reject explicitly", async () => {
  const host = linuxHost();
  for (const name of ["animateWindowClose", "dragPreviewBegin", "dragPreviewPage", "dragPreviewUpdate", "dragPreviewPlaceholder", "dragPreviewEnd", "dragPreviewCancel", "cancelFocusMove", "replyToTerminate"]) {
    assert.equal(await resolveHostModule(host, "ArdiaShell", name)(), undefined, name);
  }
  assert.equal(resolveHostModule(host, "ArdiaShell", "switcherPointerMoved")(), false);
  for (const name of ["fetchBangList", "startDictation", "pickFiles"]) {
    await assert.rejects(resolveHostModule(host, "ArdiaShell", name)(null), { code: "ERR_UNAVAILABLE" }, name);
  }
});

test("a tab drag's haptic tick settles quietly on Linux, which has no haptics", async () => {
  const host = linuxHost();
  let invoked = false;
  host.invoke = async () => { invoked = true; throw new Error("Unsupported Linux host method: ArdiaSystem.hapticTick"); };
  assert.equal(await resolveHostModule(host, "ArdiaSystem", "hapticTick")(), undefined);
  assert.equal(invoked, false);
});
