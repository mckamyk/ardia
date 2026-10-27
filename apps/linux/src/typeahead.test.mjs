import assert from "node:assert/strict";
import test from "node:test";
import { announceTypeaheadReady } from "./typeahead.ts";

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
  announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => active });
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
    announceTypeaheadReady(host, { frame: clock.frame, now: clock.now, active: () => active });
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
