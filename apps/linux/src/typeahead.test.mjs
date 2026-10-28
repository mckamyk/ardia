import assert from "node:assert/strict";
import test from "node:test";
import { announceTypeaheadReady, windowExpectsField } from "./typeahead.ts";

function fakeHost(capabilities = ["ArdiaShell.typeaheadReady"]) {
  const calls = [];
  return {
    calls,
    bootstrap: { windowId: "w-2", profileId: "default", capabilities },
    invoke: async (method, args) => { calls.push([method, args]); return null; },
    call: async () => "null",
    onEvent: () => () => {},
  };
}

function frames() {
  const queue = [];
  let time = 0;
  return {
    frame: (callback) => queue.push(callback),
    now: () => time,
    step(ms = 16) { time += ms; const next = queue.splice(0); for (const callback of next) callback(); },
    pending: () => queue.length,
  };
}

const tick = () => new Promise(setImmediate);

test("ready is announced once a text field holds focus, so held keys land in it", async () => {
  const host = fakeHost();
  const clock = frames();
  let active = { tagName: "DIV" };
  announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => active, idle: (callback) => callback() });
  clock.step();
  clock.step();
  await tick();
  assert.deepEqual(host.calls, []);
  active = { tagName: "INPUT", type: "text" };
  clock.step();
  await tick();
  assert.deepEqual(host.calls, [["ArdiaShell.typeaheadReady", { args: [true] }]]);
  assert.equal(clock.pending(), 0);
});

test("without a focused field the held keys are discarded, never sent to a page", async () => {
  const host = fakeHost();
  const clock = frames();
  announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => null });
  for (let i = 0; i < 40 && clock.pending(); i++) clock.step(16);
  await tick();
  assert.deepEqual(host.calls, [["ArdiaShell.typeaheadReady", { args: [false] }]]);
  assert.equal(clock.pending(), 0);
});

test("text areas, editable content and search fields count as typing targets, buttons don't", async () => {
  for (const [active, expected] of [
    [{ tagName: "TEXTAREA" }, 1],
    [{ tagName: "DIV", isContentEditable: true }, 1],
    [{ tagName: "INPUT", type: "search" }, 1],
    [{ tagName: "INPUT", type: "checkbox" }, 0],
    [{ tagName: "BUTTON" }, 0],
  ]) {
    const host = fakeHost();
    const clock = frames();
    announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => active, idle: (callback) => callback() });
    clock.step();
    await tick();
    assert.deepEqual(host.calls.map(([, args]) => args.args[0]), expected ? [true] : [], JSON.stringify(active));
  }
});

test("a host without held typeahead is never asked", async () => {
  const host = fakeHost([]);
  const clock = frames();
  announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => ({ tagName: "INPUT", type: "text" }) });
  clock.step();
  await tick();
  assert.deepEqual(host.calls, []);
  assert.equal(clock.pending(), 0);
});

function idles() {
  const queue = [];
  return {
    idle: (callback) => queue.push(callback),
    settle() { const next = queue.splice(0); for (const callback of next) callback(); },
    pending: () => queue.length,
  };
}

test("a window that will show a field keeps holding keys until the field focuses, however late", async () => {
  const host = fakeHost();
  const clock = frames();
  const quiet = idles();
  let active = { tagName: "BODY" };
  announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => active, idle: quiet.idle, expectsField: () => true });
  for (let i = 0; i < 200 && clock.pending(); i++) clock.step(16);
  await tick();
  assert.deepEqual(host.calls, []);
  active = { tagName: "INPUT", type: "text" };
  clock.step();
  quiet.settle();
  await tick();
  assert.deepEqual(host.calls, [["ArdiaShell.typeaheadReady", { args: [true] }]]);
});

test("the held keys are released only once the window's UI is idle after the field focuses", async () => {
  const host = fakeHost();
  const clock = frames();
  const quiet = idles();
  announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => ({ tagName: "INPUT", type: "text" }), idle: quiet.idle, expectsField: () => true });
  clock.step();
  await tick();
  assert.deepEqual(host.calls, []);
  assert.equal(quiet.pending(), 1);
  quiet.settle();
  await tick();
  assert.deepEqual(host.calls, [["ArdiaShell.typeaheadReady", { args: [true] }]]);
});

test("a field that loses focus before the UI settles is checked again", async () => {
  const host = fakeHost();
  const clock = frames();
  const quiet = idles();
  let active = { tagName: "INPUT", type: "text" };
  announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => active, idle: quiet.idle, expectsField: () => true });
  clock.step();
  active = { tagName: "BODY" };
  quiet.settle();
  await tick();
  assert.deepEqual(host.calls, []);
  active = { tagName: "INPUT", type: "search" };
  clock.step();
  quiet.settle();
  await tick();
  assert.deepEqual(host.calls, [["ArdiaShell.typeaheadReady", { args: [true] }]]);
});

test("a window expecting a field gives up before the host's own deadline", async () => {
  const host = fakeHost();
  const clock = frames();
  announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => null, idle: idles().idle, expectsField: () => true });
  for (let i = 0; i < 1000 && clock.pending(); i++) clock.step(16);
  await tick();
  assert.deepEqual(host.calls, [["ArdiaShell.typeaheadReady", { args: [false] }]]);
  assert.ok(clock.now() >= 8000 && clock.now() < 10000);
});

test("a window showing a page still gives up after half a second", async () => {
  const host = fakeHost();
  const clock = frames();
  announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => null, idle: idles().idle, expectsField: () => false });
  for (let i = 0; i < 1000 && clock.pending(); i++) clock.step(16);
  await tick();
  assert.deepEqual(host.calls, [["ArdiaShell.typeaheadReady", { args: [false] }]]);
  assert.ok(clock.now() >= 500 && clock.now() < 600);
});

test("a window expects a field while it has no active tab or its active tab is a new tab", () => {
  const state = (tabs, active) => ({ windows: { w: { id: "w", profileId: "p", activeTabIds: active ? { p: active } : {} } }, tabs });
  assert.equal(windowExpectsField(state({}, null), "w"), true);
  assert.equal(windowExpectsField(state({ t: { id: "t", windowId: "w", url: "" } }, "t"), "w"), true);
  assert.equal(windowExpectsField(state({ t: { id: "t", windowId: "w", url: "https://ardia.test/" } }, "t"), "w"), false);
  assert.equal(windowExpectsField({ windows: {}, tabs: {} }, "w"), true);
});
