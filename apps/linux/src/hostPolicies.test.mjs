import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import {
  TAB_POLICY_LOCK, isTabPolicyOwner, policyToastTarget, resetLinuxTabPoliciesForTests, sharedBatterySaver, startLinuxTabPolicies, watchHostInputActivity,
} from "./hostPolicies.ts";
import { showToast } from "./policyToasts.ts";
import { useToasts } from "../../browser/src/state/splits/actions.ts";

const source = (path) => readFileSync(new URL(path, import.meta.url), "utf8");

function inputTarget() {
  const listeners = new Map();
  return { listeners, addEventListener: (type, listener) => listeners.set(type, listener), removeEventListener: (type) => listeners.delete(type) };
}

function engineEvents() {
  const listeners = new Set();
  return {
    listeners,
    addListener: (event, listener) => {
      assert.equal(event, "engine");
      listeners.add(listener);
      return { remove: () => listeners.delete(listener) };
    },
    emit: (payload) => { for (const listener of [...listeners]) listener(payload); },
  };
}

function pendingLock() {
  const requests = [];
  return {
    requests,
    request: (name, callback) => {
      requests.push({ name, callback });
      return new Promise(() => {});
    },
    grant: () => requests.shift()?.callback(),
  };
}

function broadcast() {
  const peers = [];
  const make = () => {
    const listeners = [];
    const channel = {
      sent: [],
      postMessage: (data) => {
        channel.sent.push(data);
        for (const peer of peers) if (peer !== channel) for (const listener of peer.listeners) listener({ data });
      },
      addEventListener: (_type, listener) => listeners.push(listener),
      close: () => {},
      listeners,
    };
    peers.push(channel);
    return channel;
  };
  return make;
}

function deps(overrides = {}) {
  let clock = 1_000_000;
  const counts = { lifecycles: 0, trims: 0 };
  const providers = [];
  const engine = engineEvents();
  return {
    counts, providers, engine, advance: (ms) => { clock += ms; },
    value: {
      target: inputTarget(), now: () => clock,
      provide: (provider) => { providers.push(provider); return () => providers.splice(providers.indexOf(provider), 1); },
      startLifecycle: () => ++counts.lifecycles, startTrim: () => ++counts.trims,
      activity: (listener) => watchHostInputActivity(engine, listener),
      ...overrides,
    },
  };
}

test("native input activity from page views resets the shared idle clock", () => {
  resetLinuxTabPoliciesForTests();
  const setup = deps();
  const stop = startLinuxTabPolicies(setup.value);
  const idle = setup.providers[0]("ArdiaEngine", "inputIdleSeconds");
  setup.advance(5000);
  assert.equal(idle(), 5);
  setup.engine.emit({ name: "engine.systemState", data: {} });
  assert.equal(idle(), 5);
  setup.engine.emit({ name: "engine.inputActivity", data: {} });
  assert.equal(idle(), 0);
  setup.advance(1500);
  assert.equal(idle(), 1.5);
  stop();
  assert.equal(setup.engine.listeners.size, 0);
});

test("only the window holding the tab-policy lock runs the app-wide memory trim", () => {
  resetLinuxTabPoliciesForTests();
  const locks = pendingLock();
  const setup = deps({ locks });
  startLinuxTabPolicies(setup.value);
  assert.equal(setup.counts.lifecycles, 1, "every window still manages its own tabs");
  assert.equal(setup.counts.trims, 0);
  assert.equal(isTabPolicyOwner(), false);
  assert.deepEqual(locks.requests.map((r) => r.name), [TAB_POLICY_LOCK]);
  const held = locks.grant();
  assert.ok(held instanceof Promise, "the lock is held for the window's lifetime");
  assert.equal(isTabPolicyOwner(), true);
  assert.equal(setup.counts.trims, 1);

  resetLinuxTabPoliciesForTests();
  const unavailable = deps({ locks: { request: () => Promise.reject(new Error("no locks")) } });
  startLinuxTabPolicies(unavailable.value);
  return new Promise((resolve) => setTimeout(resolve, 0)).then(() => {
    assert.equal(isTabPolicyOwner(), true, "without Web Locks the window runs everything itself");
    assert.equal(unavailable.counts.trims, 1);
  });
});

test("battery saver toasts appear only in the renderer that owns the focused window", () => {
  resetLinuxTabPoliciesForTests();
  assert.equal(policyToastTarget("w1"), "w1", "no window binding keeps the shared behaviour");
  const setup = deps({ windowId: "mine" });
  startLinuxTabPolicies(setup.value);
  useToasts.setState({ toasts: {} });
  showToast("someone-else", "Battery Saver Mode Activated", "Busy background tabs are paused.", { icon: "battery.25" });
  assert.deepEqual(useToasts.getState().toasts, {}, "another window's renderer shows it");
  assert.equal(policyToastTarget("mine"), "mine");
  showToast("mine", "Battery Saver Mode Activated", "Busy background tabs are paused.", { icon: "battery.25" });
  assert.equal(useToasts.getState().toasts.mine?.title, "Battery Saver Mode Activated");
});

test("engine.batterySaver is applied by the owner and shared with the other windows", async () => {
  const make = broadcast();
  const ownerChannel = make();
  const otherChannel = make();
  resetLinuxTabPoliciesForTests();
  startLinuxTabPolicies(deps({ channel: otherChannel, locks: pendingLock() }).value);
  ownerChannel.postMessage({ mode: "on", reply: { state: 3, active: true } });
  let applied = 0;
  const apply = async () => { ++applied; return { state: 0, active: false }; };
  assert.deepEqual(await sharedBatterySaver("on", apply), { state: 3, active: true });
  assert.equal(applied, 0, "a window that is not the owner reuses the owner's reply");
  const waiting = sharedBatterySaver("off", apply, 1000);
  ownerChannel.postMessage({ mode: "off", reply: { state: 0, active: false } });
  assert.deepEqual(await waiting, { state: 0, active: false });
  assert.equal(applied, 0);
  assert.deepEqual(await sharedBatterySaver("default", apply, 5), { state: 0, active: false });
  assert.equal(applied, 1, "without an owner reply in time the window applies it itself");
  otherChannel.listeners.forEach((listener) => listener({ data: { mode: 1 } }));

  resetLinuxTabPoliciesForTests();
  const locks = pendingLock();
  startLinuxTabPolicies(deps({ channel: ownerChannel, locks }).value);
  locks.grant();
  ownerChannel.sent.length = 0;
  assert.deepEqual(await sharedBatterySaver("on", async () => ({ state: 3, active: true })), { state: 3, active: true });
  assert.deepEqual(ownerChannel.sent, [{ mode: "on", reply: { state: 3, active: true } }]);
});

test("the Linux build routes tabLifecycle's toasts and battery saver through the policy adapters", () => {
  const lifecycle = source("../../browser/src/state/tabs/tabLifecycle.ts");
  assert.match(lifecycle, /import \{ showToast \} from "\.\.\/splits\/actions";/);
  assert.match(lifecycle, /import \{[^}]*setBatterySaver[^}]*\} from "\.\.\/\.\.\/platform\/performance";/);
  const vite = source("../vite.config.ts");
  assert.ok(vite.includes('[`${here("../browser/src/state/splits/actions.ts")}\\0${tabLifecycle}`, here("./src/policyToasts.ts")]'));
  assert.ok(vite.includes('[`${here("../browser/src/platform/performance.ts")}\\0${tabLifecycle}`, here("./src/policyPerformance.ts")]'));
  const main = source("./main.tsx");
  for (const seam of ["windowId, locks: navigator.locks", "new BroadcastChannel(TAB_POLICY_CHANNEL)", "watchHostInputActivity(requireNativeModule(\"ArdiaEngine\"), listener)"]) {
    assert.ok(main.includes(seam), seam);
  }
});
