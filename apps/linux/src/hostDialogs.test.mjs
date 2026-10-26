import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";
import { createHostDialogs } from "./hostDialogs.ts";

const turn = () => new Promise((resolve) => setImmediate(resolve));
const deferred = () => {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
};
function fixture(windowId = "a", generation = "9007199254740993") {
  const listeners = new Set();
  const closing = new Set();
  const requests = [];
  let sequence = 0;
  const host = {
    bootstrap: { windowId, profileId: "default" },
    onEvent(listener) { listeners.add(listener); return () => listeners.delete(listener); },
    onBeforeClose(listener) { closing.add(listener); return () => closing.delete(listener); },
    async invoke(method, data) {
      requests.push({ method, ...data });
      return method === "ArdiaShell.windowIdentity" ? { windowId, rendererGeneration: generation } : { accepted: true };
    },
  };
  const dialogs = createHostDialogs(host, () => `dialog:test-identity-${++sequence}`);
  const emit = (result, overrides = {}) => {
    const request = requests.findLast((r) => r.method.endsWith("beginDialog"));
    const data = { dialogId: request?.args[0].dialogId, windowId, rendererGeneration: generation, result, ...overrides };
    for (const listener of listeners) listener("ArdiaShell", "shell", { name: "shell.dialogResolved", data });
  };
  return { host, dialogs, requests, listeners, closing, emit };
}

test("confirm preserves presentation, destructive label and checkbox on Cancel", async () => {
  const f = fixture();
  const options = { title: "Delete Profile?", message: "Cannot undo", confirmTitle: "Delete Profile", cancelTitle: "Keep Profile", destructive: true, suppression: "Don’t ask me again", windowId: "a" };
  const work = f.dialogs.confirm(options);
  await turn();
  assert.deepEqual(f.requests[1].args[0], { dialogId: "dialog:test-identity-1:1", kind: "confirm", options });
  f.emit({ confirmed: false, suppressed: true });
  assert.deepEqual(await work, { confirmed: false, suppressed: true });
  f.dialogs.retire();
});

test("prompt trims nonempty text and returns null for empty input or cancel", async () => {
  const f = fixture();
  for (const [reply, expected] of [["  Renamed\n", "Renamed"], ["\u2003 \n", null], [null, null]]) {
    const work = f.dialogs.prompt({ title: "Rename", value: "Old", placeholder: "Name", confirmTitle: "Update" });
    await turn(); f.emit(reply);
    assert.equal(await work, expected);
  }
  f.dialogs.retire();
});

test("result before ack buffers once; failed acknowledgement never accepts it", async () => {
  for (const fail of [false, true]) {
    const f = fixture();
    let ack;
    f.host.invoke = (method, data) => {
      f.requests.push({ method, ...data });
      if (method.endsWith("windowIdentity")) return Promise.resolve({ windowId: "a", rendererGeneration: "9007199254740993" });
      if (method.endsWith("cancelDialog")) return Promise.resolve();
      f.emit({ confirmed: true, suppressed: true });
      f.emit({ confirmed: false, suppressed: false });
      return new Promise((resolve, reject) => { ack = fail ? () => reject(new Error("begin failed")) : () => resolve({ accepted: true }); });
    };
    let settled = false;
    const work = f.dialogs.confirm({ title: "Confirm", suppression: "Hide" });
    void work.then(() => { settled = true; }, () => { settled = true; });
    await turn(); assert.equal(settled, false); ack();
    if (fail) { await assert.rejects(work, /begin failed/); assert.equal(f.requests.at(-1).method, "ArdiaShell.cancelDialog"); }
    else assert.deepEqual(await work, { confirmed: true, suppressed: true });
    f.dialogs.retire();
  }
});

test("two windows reject wrong owner, wrong generation, malformed and duplicate results", async () => {
  const a = fixture("a", "1"), b = fixture("b", "2");
  let count = 0;
  const wa = a.dialogs.confirm({ title: "A" }).then((value) => { count++; return value; });
  const wb = b.dialogs.prompt({ title: "B" });
  await turn();
  a.emit({ confirmed: true, suppressed: false }, { windowId: "b" });
  a.emit({ confirmed: true, suppressed: false }, { rendererGeneration: "2" });
  a.emit({ confirmed: "yes", suppressed: false });
  a.emit({ confirmed: true, suppressed: false }, { rendererGeneration: 1 });
  await turn(); assert.equal(count, 0);
  a.emit({ confirmed: false, suppressed: true });
  a.emit({ confirmed: true, suppressed: false });
  b.emit("B value");
  assert.deepEqual(await wa, { confirmed: false, suppressed: false });
  assert.equal(await wb, "B value"); assert.equal(count, 1);
  await assert.rejects(a.dialogs.prompt({ title: "Wrong", windowId: "b" }), /owner/);
  a.dialogs.retire(); b.dialogs.retire();
});

test("public dialog remains pending beyond transport deadline after immediate begin ack", async (t) => {
  t.mock.timers.enable({ apis: ["setTimeout", "Date"] });
  const f = fixture();
  let settled = false, transportPending = 0;
  f.host.invoke = async (method, data) => {
    f.requests.push({ method, ...data });
    transportPending++;
    try { return method.endsWith("windowIdentity") ? { windowId: "a", rendererGeneration: "9007199254740993" } : { accepted: true }; } finally { transportPending--; }
  };
  const work = f.dialogs.confirm({ title: "Wait", destructive: true }).then((value) => { settled = true; return value; });
  await turn();
  t.mock.timers.tick(120_000);
  await turn();
  assert.equal(transportPending, 0); assert.equal(settled, false);
  assert.equal(f.requests.length, 2);
  f.emit({ confirmed: false, suppressed: false });
  assert.deepEqual(await work, { confirmed: false, suppressed: false });
  f.dialogs.retire();
});

test("real bootstrap clears the 30s begin timer while the public decision stays pending", async () => {
  const timers = new Map(), events = new Map(), calls = [];
  let timerId = 0;
  const context = {
    location: { protocol: "chrome:", hostname: "ardia" },
    crypto, console,
    __ardiaBootstrap: { windowId: "a", profileId: "default" },
    setTimeout(callback, delay) { const id = ++timerId; timers.set(id, { callback, delay }); return id; },
    clearTimeout(id) { timers.delete(id); },
    addEventListener(name, callback) { events.set(name, callback); },
    chrome: { send(_channel, [id, transport, data]) {
      calls.push({ transport, ...data });
      context.__ardiaReply(id, true, data.method.endsWith("windowIdentity") ? { windowId: "a", rendererGeneration: "1" } : { accepted: true });
    } },
  };
  context.window = context; context.top = context;
  const source = readFileSync(new URL("../../../engine/ardiacore-linux/src/ardia/linux/bootstrap.js", import.meta.url), "utf8");
  runInNewContext(source, context);
  const adapter = createHostDialogs(context.__ardiaHost);
  let settled = false;
  const work = adapter.confirm({ title: "Wait for user", destructive: true }).then((result) => { settled = true; return result; });
  await turn();
  assert.equal(calls[0].transport, "host.invoke");
  assert.equal(calls[0].method, "ArdiaShell.windowIdentity");
  assert.deepEqual([...calls[0].args.args], []);
  assert.equal(calls[1].method, "ArdiaShell.beginDialog");
  assert.equal(timers.size, 0);
  for (const timer of timers.values()) if (timer.delay <= 120_000) timer.callback();
  await turn(); assert.equal(settled, false);
  const { dialogId } = calls[1].args.args[0];
  context.__ardiaEvent("ArdiaShell", "shell", { name: "shell.dialogResolved", data: { dialogId, windowId: "a", rendererGeneration: "1", result: { confirmed: false, suppressed: false } } });
  assert.deepEqual(await work, { confirmed: false, suppressed: false });
  const canceled = adapter.prompt({ title: "Pending on teardown" });
  await turn(); events.get("pagehide")();
  assert.equal(await canceled, null);
});

test("bounded queue, retry after failure and retirement safely cancel pending dialogs", async () => {
  const f = fixture();
  const pending = Array.from({ length: 16 }, () => f.dialogs.confirm({ title: "Queue", suppression: "Hide" }));
  await assert.rejects(f.dialogs.confirm({ title: "Overflow" }), /Too many/);
  await turn();
  for (const close of [...f.closing]) close();
  assert.deepEqual(await Promise.all(pending), pending.map(() => ({ confirmed: false, suppressed: false })));
  assert.equal(f.listeners.size, 0);
  await assert.rejects(f.dialogs.prompt({ title: "Retired" }), /unavailable/);
  const retry = fixture();
  const original = retry.host.invoke;
  retry.host.invoke = async () => { throw new Error("failed"); };
  await assert.rejects(retry.dialogs.prompt({ title: "Try" }), /failed/);
  retry.host.invoke = original;
  const work = retry.dialogs.prompt({ title: "Retry" });
  await turn(); retry.emit("ok"); assert.equal(await work, "ok");
  retry.dialogs.retire();
});

test("bad begin acknowledgements reject and cancel registration", async () => {
  for (const reply of [null, false, {}, { accepted: false }]) {
    const f = fixture();
    f.host.invoke = async (method, payload) => {
      f.requests.push({ method, ...payload });
      return method.endsWith("windowIdentity") ? { windowId: "a", rendererGeneration: "9007199254740993" } : reply;
    };
    await assert.rejects(f.dialogs.confirm({ title: "Refused", destructive: true }), /not accepted/);
    await turn();
    assert.equal(f.requests.at(-1).method, "ArdiaShell.cancelDialog");
    f.dialogs.retire();
  }
});

test("retirement after early result suppresses a late begin acknowledgement", async () => {
  const f = fixture();
  let ack;
  f.host.invoke = (method, data) => {
    f.requests.push({ method, ...data });
    if (method.endsWith("windowIdentity")) return Promise.resolve({ windowId: "a", rendererGeneration: "9007199254740993" });
    return method.endsWith("beginDialog") ? new Promise((resolve) => { ack = resolve; }) : Promise.resolve();
  };
  const work = f.dialogs.confirm({ title: "Old", suppression: "Hide" });
  await turn(); f.emit({ confirmed: true, suppressed: true });
  f.dialogs.retire();
  await assert.rejects(f.dialogs.prompt({ title: "Retired" }), /unavailable/);
  ack({ accepted: true });
  assert.deepEqual(await work, { confirmed: false, suppressed: false });
});

test("invalid queried generations fail and repeated entropy cannot reuse a completed identity", async () => {
  for (const generation of [undefined, 0, 1, "0", "01", "-1", "x", "1".repeat(21)]) {
    const f = fixture();
    f.host.invoke = async () => ({ windowId: "a", rendererGeneration: generation });
    await assert.rejects(f.dialogs.prompt({ title: "Invalid" }), /Invalid native dialog window identity/);
    f.dialogs.retire();
  }
  const f = fixture();
  f.dialogs.retire();
  const adapter = createHostDialogs(f.host, () => "dialog:constant-identity");
  const work = adapter.prompt({ title: "First" });
  await turn();
  const first = f.requests.at(-1).args[0].dialogId;
  f.emit("First"); assert.equal(await work, "First");
  const second = adapter.prompt({ title: "Second" });
  await turn();
  assert.notEqual(f.requests.at(-1).args[0].dialogId, first);
  let settled = false;
  void second.then(() => { settled = true; });
  f.emit("Stale", { dialogId: first });
  await turn(); assert.equal(settled, false);
  adapter.retire(); assert.equal(await second, null);
});

test("listener precedes async identity and early begin result uses the queried generation", async () => {
  const f = fixture();
  const identity = deferred(), ack = deferred();
  f.host.invoke = (method, data) => {
    assert.equal(f.listeners.size, 1);
    f.requests.push({ method, ...data });
    if (method.endsWith("windowIdentity")) {
      assert.deepEqual(data, { args: [] });
      return identity.promise;
    }
    assert.equal(method, "ArdiaShell.beginDialog");
    f.emit({ confirmed: true, suppressed: true }, { rendererGeneration: "stale" });
    f.emit({ confirmed: false, suppressed: true });
    return ack.promise;
  };
  let settled = false;
  const work = f.dialogs.confirm({ title: "Async", suppression: "Hide" }).then((value) => { settled = true; return value; });
  await turn();
  assert.equal(f.requests.length, 1);
  f.emit({ confirmed: true, suppressed: true }, { dialogId: "dialog:test-identity-1:1" });
  identity.resolve({ windowId: "a", rendererGeneration: "9007199254740993" });
  await turn(); assert.equal(settled, false);
  ack.resolve({ accepted: true });
  assert.deepEqual(await work, { confirmed: false, suppressed: true });
  f.dialogs.retire();
});

test("malformed identity and unsupported backend reject without begin and release every slot", async () => {
  const f = fixture();
  const original = f.host.invoke;
  for (const reply of [null, false, "a", [], {}, { windowId: "b", rendererGeneration: "1" }, { windowId: "a" }]) {
    for (let attempt = 0; attempt < 17; attempt++) {
      f.host.invoke = async (method, data) => { f.requests.push({ method, ...data }); return reply; };
      await assert.rejects(f.dialogs.prompt({ title: "Invalid identity" }), { code: "ERR_DIALOG", message: "Invalid native dialog window identity" });
    }
  }
  f.host.invoke = async (method, data) => {
    f.requests.push({ method, ...data });
    throw Object.assign(new Error("Unsupported ArdiaShell.windowIdentity"), { code: "ERR_UNAVAILABLE" });
  };
  await assert.rejects(f.dialogs.confirm({ title: "Unsupported", destructive: true }), { code: "ERR_UNAVAILABLE" });
  assert.ok(f.requests.every((request) => request.method === "ArdiaShell.windowIdentity"));
  f.host.invoke = original;
  const retry = f.dialogs.prompt({ title: "Retry after failure" });
  await turn(); f.emit("Retry"); assert.equal(await retry, "Retry");
  f.dialogs.retire();
});

test("late identity success or failure after retirement cannot dispatch begin or alter replacement", async () => {
  for (const succeeds of [true, false]) {
    const f = fixture();
    const identity = deferred();
    const original = f.host.invoke;
    f.host.invoke = (method, data) => { f.requests.push({ method, ...data }); return identity.promise; };
    const old = f.dialogs.confirm({ title: "Old", suppression: "Hide" });
    await turn(); f.dialogs.retire();
    assert.deepEqual(await old, { confirmed: false, suppressed: false });
    f.host.invoke = original;
    const replacement = createHostDialogs(f.host);
    const current = replacement.prompt({ title: "Replacement" });
    await turn();
    if (succeeds) identity.resolve({ windowId: "a", rendererGeneration: "9007199254740992" });
    else identity.reject(new Error("Retired query failed"));
    await turn();
    assert.equal(f.requests.filter((request) => request.method.endsWith("beginDialog")).length, 1);
    assert.equal(f.requests.filter((request) => request.method.endsWith("cancelDialog")).length, 0);
    f.emit("Replacement"); assert.equal(await current, "Replacement");
    replacement.retire();
  }
});

test("retirement after identity validation and before dispatch cancels without native begin", async () => {
  const f = fixture();
  let reads = 0;
  f.host.invoke = async (method, data) => {
    f.requests.push({ method, ...data });
    return { windowId: "a", get rendererGeneration() {
      if (++reads === 2) queueMicrotask(() => f.dialogs.retire());
      return "9007199254740993";
    } };
  };
  const work = f.dialogs.prompt({ title: "Retire before begin" });
  assert.equal(await work, null);
  assert.equal(reads, 2);
  assert.deepEqual(f.requests.map((request) => request.method), ["ArdiaShell.windowIdentity"]);
});

test("each begin queries anew and native authority rejection cannot accept a stale result", async () => {
  const f = fixture();
  let queries = 0;
  f.host.invoke = async (method, data) => {
    f.requests.push({ method, ...data });
    if (method.endsWith("windowIdentity")) return { windowId: "a", rendererGeneration: String(++queries) };
    if (method.endsWith("beginDialog") && queries === 1) {
      f.emit({ confirmed: true, suppressed: true }, { rendererGeneration: "1" });
      throw Object.assign(new Error("Committed document changed"), { code: "ERR_DIALOG_OWNER" });
    }
    return { accepted: true };
  };
  await assert.rejects(f.dialogs.confirm({ title: "Old authority", destructive: true, suppression: "Hide" }), { code: "ERR_DIALOG_OWNER" });
  const retry = f.dialogs.confirm({ title: "New authority", suppression: "Hide" });
  let settled = false;
  void retry.then(() => { settled = true; });
  await turn();
  f.emit({ confirmed: true, suppressed: true }, { rendererGeneration: "1" });
  await turn(); assert.equal(settled, false);
  f.emit({ confirmed: false, suppressed: true }, { rendererGeneration: "2" });
  assert.deepEqual(await retry, { confirmed: false, suppressed: true });
  assert.equal(queries, 2);
  f.dialogs.retire();
});

test("identity awaits count toward the bounded pending queue", async () => {
  const f = fixture();
  const identity = deferred();
  f.host.invoke = (method, data) => { f.requests.push({ method, ...data }); return identity.promise; };
  const pending = Array.from({ length: 16 }, () => f.dialogs.prompt({ title: "Waiting for identity" }));
  await assert.rejects(f.dialogs.prompt({ title: "Overflow" }), /Too many/);
  await turn();
  assert.equal(f.requests.length, 16);
  assert.ok(f.requests.every((request) => request.method.endsWith("windowIdentity")));
  f.dialogs.retire();
  assert.deepEqual(await Promise.all(pending), Array(16).fill(null));
  identity.resolve({ windowId: "a", rendererGeneration: "1" });
  await turn(); assert.equal(f.requests.length, 16);
});
