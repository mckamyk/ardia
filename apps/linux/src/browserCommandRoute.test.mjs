import assert from "node:assert/strict";
import test from "node:test";
import { BROWSER_WINDOW_COMMANDS, routeToBrowserWindow, serveBrowserCommands } from "./browserCommandRoute.ts";

function focusTarget(focused) {
  const listeners = [];
  return { addEventListener: (_type, listener) => listeners.push(listener), hasFocus: () => focused, focus: () => listeners.forEach((listener) => listener()) };
}

function browserRenderer(windowId, { focused = false, clock }) {
  const channel = new BroadcastChannel(`route-test-${process.pid}`);
  const target = focusTarget(focused);
  const ran = [];
  const focusedWindows = [];
  const served = serveBrowserCommands({ windowId, channel, target, now: clock, run: (id, command) => ran.push([id, command]), focus: async (id) => focusedWindows.push(id) });
  return { channel, target, ran, focusedWindows, close: () => { served.remove(); channel.close(); } };
}

test("a utility window's browser command runs once, in the most recently focused browser window", async () => {
  let time = 100;
  const clock = () => time++;
  const first = browserRenderer("w1", { focused: true, clock });
  const second = browserRenderer("w2", { clock });
  const third = browserRenderer("w3", { clock });
  second.target.focus();
  const settings = new BroadcastChannel(`route-test-${process.pid}`);
  try {
    const ran = await routeToBrowserWindow({ channel: settings, command: "showOnboarding", gatherMs: 50 });
    assert.equal(ran, "w2");
    assert.deepEqual([first.ran, second.ran, third.ran], [[], [["w2", "showOnboarding"]], []]);
    assert.deepEqual(second.focusedWindows, ["w2"]);
    third.target.focus();
    assert.equal(await routeToBrowserWindow({ channel: settings, command: "toolTour", gatherMs: 50 }), "w3");
    assert.deepEqual(third.ran, [["w3", "toolTour"]]);
  } finally {
    [first, second, third].forEach((renderer) => renderer.close());
    settings.close();
  }
});

test("with no browser window alive the route answers null instead of pretending it ran", async () => {
  const settings = new BroadcastChannel(`route-test-${process.pid}`);
  try {
    assert.equal(await routeToBrowserWindow({ channel: settings, command: "showOnboarding", gatherMs: 30 }), null);
  } finally {
    settings.close();
  }
});

test("browser windows refuse routed commands that aren't browser-window help commands", async () => {
  const browser = browserRenderer("w1", { focused: true, clock: Date.now });
  const settings = new BroadcastChannel(`route-test-${process.pid}`);
  try {
    assert.equal(BROWSER_WINDOW_COMMANDS.has("quit"), false);
    assert.equal(await routeToBrowserWindow({ channel: settings, command: "quit", gatherMs: 30, answerMs: 100 }), null);
    assert.deepEqual(browser.ran, []);
  } finally {
    browser.close();
    settings.close();
  }
});
