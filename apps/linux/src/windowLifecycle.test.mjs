import assert from "node:assert/strict";
import test from "node:test";

globalThis.__DEV__ = false;
const { useBrowser } = await import("../../browser/src/state/browser/store.ts");
const { createWindowSync } = await import("../../browser/src/state/windows/windowSync.ts");
const { screenPoint, windowUnder } = await import("../../browser/src/state/drag/windowDrop.ts");
const { initialLinuxSession } = await import("./session.ts");

const S = () => useBrowser.getState();

function fakePort() {
  const opens = [];
  let listener = () => {};
  return {
    opens,
    emit: (event) => listener(event),
    port: {
      open: (id, options) => opens.push([id, options]),
      close: () => {},
      setTitle: () => {},
      windowIds: async () => [],
      restoreFullScreen: () => {},
      onEvent: (l) => void (listener = l),
      releaseProfile: () => {},
    },
  };
}

function startRenderer(windowId, saved) {
  S().hydrate(initialLinuxSession({ bootstrap: { windowId, profileId: "default", startup: false } }, saved));
  const native = fakePort();
  const sync = createWindowSync({ port: native.port, getState: S, isQuitting: () => false, lightsCenter: () => null, isUtilityWindow: (id) => id === "settings" });
  sync.listen();
  sync.sync(S());
  return { native, sync };
}

test("a renderer's first sync asks to focus its own window, which the native ledger treats as a launch echo", () => {
  const main = initialLinuxSession({ bootstrap: { windowId: "main", profileId: "default", startup: false } }, null);
  const { native } = startRenderer("window-2", main);
  const own = native.opens.find(([id]) => id === "window-2");
  const other = native.opens.find(([id]) => id === "main");
  assert.equal(own[1].focus, true);
  assert.equal(other[1].focus, false);
});

test("Ctrl+N asks the native host to focus the new regular window", () => {
  const { native, sync } = startRenderer("main", null);
  const before = S();
  const id = S().createWindow();
  sync.sync(S(), before);
  const opened = native.opens.find(([opened]) => opened === id);
  assert.equal(opened[1].focus, true);
  assert.equal(opened[1].behind, undefined);
});

test("background window creation keeps focus where it was", () => {
  const { native, sync } = startRenderer("main", null);
  const before = S();
  const id = S().createWindow({ background: true });
  sync.sync(S(), before);
  assert.equal(native.opens.find(([opened]) => opened === id)[1].focus, false);
  assert.equal(S().ui.focusedWindowId, "main");
});

test("native focus, frame and fullScreen events update the renderer's store", () => {
  const saved = initialLinuxSession({ bootstrap: { windowId: "main", profileId: "default", startup: false } }, null);
  const { native } = startRenderer("window-2", saved);
  assert.equal(S().ui.focusedWindowId, "window-2");
  native.emit({ type: "focus", id: "main" });
  assert.equal(S().ui.focusedWindowId, "main");
  native.emit({ type: "frame", id: "main", frame: [0, 480, 800, 600] });
  assert.deepEqual(S().windows.main.frame, [0, 480, 800, 600]);
  native.emit({ type: "fullScreen", id: "main", on: true });
  assert.ok(S().windows.main.fullScreen > 0);
  native.emit({ type: "fullScreen", id: "main", on: false });
  assert.equal(S().windows.main.fullScreen, undefined);
  native.emit({ type: "focus", id: "settings" });
  assert.equal(S().ui.focusedWindowId, "main");
});

const PRIMARY_HEIGHT = 1080;
const topDown = { a: [0, 0, 800, 600], b: [900, 400, 800, 600] };
const sharedFrame = ([x, y, w, h]) => [x, PRIMARY_HEIGHT - (y + h), w, h];
const dragState = (frames) => ({
  windows: {
    a: { id: "a", incognito: false, kind: "browser", frame: frames.a },
    b: { id: "b", incognito: false, kind: "browser", frame: frames.b },
  },
  ui: { focusOrder: ["a", "b"] },
});

test("R02: Linux top-down frames make the shared drop code miss the window under the pointer", () => {
  const pointer = [1000, 500];
  const local = [pointer[0] - topDown.a[0], pointer[1] - topDown.a[1]];
  const legacy = windowUnder(dragState(topDown), "a", local[0], local[1], topDown.a);
  assert.deepEqual(legacy, { outside: true, overWindow: null });
});

test("R02: frames reported in the shared bottom-up contract find the window under the pointer and its local point", () => {
  const frames = { a: sharedFrame(topDown.a), b: sharedFrame(topDown.b) };
  const local = [1000 - topDown.a[0], 500 - topDown.a[1]];
  const s = dragState(frames);
  assert.deepEqual(windowUnder(s, "a", local[0], local[1], frames.a), { outside: true, overWindow: "b" });
  const screen = screenPoint(s, "a", local[0], local[1], frames.a);
  const over = frames.b;
  assert.deepEqual([screen[0] - over[0], over[1] + over[3] - screen[1]], [1000 - topDown.b[0], 500 - topDown.b[1]]);
});
