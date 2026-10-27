import assert from "node:assert/strict";
import test from "node:test";
import { createWindowCloseBarrier } from "./utilityClose.ts";

const request = { windowId: "settings", rendererGeneration: "9007199254740993", closeGeneration: "1" };
const tick = () => new Promise((resolve) => setImmediate(resolve));
const deferred = () => { let resolve, reject; const promise = new Promise((yes, no) => { resolve = yes; reject = no; }); return { promise, resolve, reject }; };

test("transport timeout leaves producers frozen until exact native cancellation event", async () => {
  const ack = deferred();
  const resumed = [];
  const close = createWindowCloseBarrier({ quiesce() {}, async drain() { return 7; }, acknowledge: () => ack.promise, async reject() { throw Object.assign(new Error("teardown continues"), { code: "ERR_WINDOW_CLOSE_IN_PROGRESS" }); }, resume: (value) => resumed.push(value) });
  const pending = close(request);
  await tick();
  ack.reject(new Error("transport timeout"));
  await assert.rejects(pending, { code: "ERR_WINDOW_CLOSE_IN_PROGRESS" });
  assert.equal(resumed.length, 0);
  assert.equal(close.cancelled({ ...request, closeGeneration: "2" }), false);
  assert.equal(close.cancelled({ ...request, rendererGeneration: 9007199254740993 }), false);
  assert.equal(close.cancelled(request), true);
  assert.equal(close.cancelled(request), true);
  assert.deepEqual(resumed, [request]);
});

test("successful rejection and cancellation event resume the same token once", async () => {
  const rejected = deferred();
  let resumed = 0;
  const close = createWindowCloseBarrier({ quiesce() {}, async drain() { throw new Error("save failed"); }, async acknowledge() {}, reject: () => rejected.promise, resume() { ++resumed; } });
  const pending = close(request);
  await tick();
  close.cancelled(request);
  rejected.resolve();
  await assert.rejects(pending, /save failed/);
  assert.equal(resumed, 1);
});

test("cancellation before producer quiescence performs no work or resumption", async () => {
  let calls = 0;
  const close = createWindowCloseBarrier({ quiesce() { ++calls; }, async drain() { ++calls; return 0; }, async acknowledge() { ++calls; }, async reject() { ++calls; }, resume() { ++calls; } });
  const pending = close(request);
  close.cancelled(request);
  await assert.rejects(pending, { code: "ERR_WINDOW_CLOSE_CANCELLED" });
  assert.equal(calls, 0);
});

test("cancellation during drain prevents late acknowledgment and stale events cannot resume a new token", async () => {
  const drain = deferred();
  const resumed = [];
  let acknowledgments = 0;
  const close = createWindowCloseBarrier({ quiesce() {}, drain: () => drain.promise, async acknowledge() { ++acknowledgments; }, async reject() {}, resume: (value) => resumed.push(value.closeGeneration) });
  const first = close(request);
  await tick();
  close.cancelled(request);
  drain.resolve(1);
  await assert.rejects(first, { code: "ERR_WINDOW_CLOSE_CANCELLED" });
  assert.equal(acknowledgments, 0);
  const next = { ...request, closeGeneration: "2" };
  const second = close(next);
  close.cancelled(request);
  assert.deepEqual(resumed, ["1"]);
  close.cancelled(next);
  await assert.rejects(second, { code: "ERR_WINDOW_CLOSE_CANCELLED" });
  assert.deepEqual(resumed, ["1"]);
});

test("duplicate old cancellation cannot resume producers quiesced for a newer attempt", async () => {
  let ack = deferred();
  const resumed = [];
  const close = createWindowCloseBarrier({ quiesce() {}, async drain() { return 0; }, acknowledge: () => ack.promise, async reject() { throw new Error("not confirmed"); }, resume: (value) => resumed.push(value.closeGeneration) });
  const first = close(request);
  await tick();
  close.cancelled(request);
  ack.reject(new Error("late old timeout"));
  await assert.rejects(first, /late old timeout/);
  ack = deferred();
  const next = { ...request, closeGeneration: "2" };
  const second = close(next);
  await tick();
  close.cancelled(request);
  assert.deepEqual(resumed, ["1"]);
  close.cancelled(next);
  ack.reject(new Error("new timeout"));
  await assert.rejects(second, /new timeout/);
  close.cancelled(next);
  assert.deepEqual(resumed, ["1", "2"]);
});

const { createWindowCloseCoordinator } = await import("./windowCloseCoordinator.ts");
const main = { windowId: "main", rendererGeneration: "9007199254740993", closeGeneration: "1" };

function harness(options = {}) {
  const log = [];
  const asks = [];
  const pending = { reject: [] };
  const barrierHooks = {
    quiesce: (value) => log.push(["quiesce", value.closeGeneration]),
    drain: options.drain ?? (async () => { log.push(["drain"]); return 3; }),
    acknowledge: options.acknowledge ?? (async (value, watermark) => { log.push(["acknowledge", value.closeGeneration, watermark]); }),
    reject: async (value) => { log.push(["barrier-reject", value.closeGeneration]); if (options.barrierReject) return options.barrierReject(); },
    resume: (value) => { log.push(["resume", value.closeGeneration]); coordinator.producersResumed(value); },
  };
  const barrier = createWindowCloseBarrier(barrierHooks);
  const coordinator = createWindowCloseCoordinator({
    windowId: "main",
    utility: !!options.utility,
    decide: (windowId, reason, ask) => {
      log.push(["decide", windowId, reason]);
      return ask({ title: "Close 2 tabs?", windowId }).then(({ confirmed, suppressed }) => ({ decision: confirmed ? "allow" : "cancel", suppressWarning: suppressed }));
    },
    ask: (options, signal) => {
      const answer = deferred();
      asks.push({ options, signal, answer });
      log.push(["ask"]);
      return answer.promise;
    },
    suppress: (reason) => log.push(["suppress", reason]),
    flush: options.flush ?? (async () => { log.push(["flush"]); }),
    request: options.request ?? (async () => main),
    beginDrain: options.beginDrain ?? (async (value) => { log.push(["begin", value.closeGeneration]); return { accepted: true, endsSession: options.endsSession ?? true }; }),
    depart: (value) => log.push(["depart", value.closeGeneration]),
    stay: (value) => log.push(["stay", value.closeGeneration]),
    reject: options.reject ?? (async (value) => { log.push(["reject", value.closeGeneration]); }),
    barrier,
    cancelled: barrier.cancelled,
    suspendDialogs: options.suspend,
    resumeDialogs: options.resume,
    report: (error) => log.push(["report", error.message]),
  });
  return { log, asks, coordinator, pending };
}
const steps = (log) => log.map(([step]) => step);

test("the warning is answered before producers quiesce, and allow drains, seals and acknowledges in order", async () => {
  const { log, asks, coordinator } = harness();
  const closing = coordinator.accept(main);
  await tick();
  assert.deepEqual(steps(log), ["decide", "ask"]);
  asks[0].answer.resolve({ confirmed: true, suppressed: true });
  await closing;
  assert.deepEqual(steps(log), ["decide", "ask", "suppress", "begin", "flush", "quiesce", "drain", "acknowledge"]);
  assert.deepEqual(log.at(-1), ["acknowledge", "1", 3]);
});

test("duplicate close events for one attempt show one warning and run one barrier", async () => {
  const { log, asks, coordinator } = harness();
  const first = coordinator.accept(main);
  const second = coordinator.accept({ ...main });
  const third = coordinator.close("window");
  assert.equal(first, second);
  await tick();
  assert.equal(asks.length, 1);
  asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await Promise.all([first, second, third]);
  assert.deepEqual(steps(log).filter((step) => step !== "ask"), ["decide", "begin", "quiesce", "drain", "acknowledge"]);
});

test("native cancellation aborts and discards an open warning; its later answer never suppresses, drains or rejects", async () => {
  const { log, asks, coordinator } = harness();
  const closing = coordinator.accept(main);
  await tick();
  assert.equal(asks[0].signal.aborted, false);
  assert.equal(coordinator.cancelled({ ...main, closeGeneration: "2" }), false);
  assert.equal(asks[0].signal.aborted, false);
  assert.equal(coordinator.cancelled(main), true);
  assert.equal(asks[0].signal.aborted, true);
  await assert.rejects(closing, { code: "ERR_WINDOW_CLOSE_CANCELLED" });
  asks[0].answer.resolve({ confirmed: true, suppressed: true });
  await tick();
  assert.deepEqual(steps(log), ["decide", "ask"]);
});

test("a late allow cannot drain once a newer attempt or cancellation superseded it", async () => {
  const { log, asks, coordinator } = harness();
  const old = coordinator.accept(main);
  await tick();
  const next = { ...main, closeGeneration: "2" };
  const current = coordinator.accept(next);
  await tick();
  asks[0].answer.resolve({ confirmed: true, suppressed: true });
  await assert.rejects(old, { code: "ERR_WINDOW_CLOSE_STALE" });
  assert.deepEqual(log.filter(([step]) => step === "begin" || step === "suppress" || step === "quiesce"), []);
  const begin = deferred();
  const racingBegins = [];
  const racing = harness({ beginDrain: (value) => { racingBegins.push(value.closeGeneration); return begin.promise; } });
  const racingClose = racing.coordinator.accept(main);
  await tick();
  racing.asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await tick();
  racing.coordinator.cancelled(main);
  begin.resolve({ accepted: true });
  await assert.rejects(racingClose, { code: "ERR_WINDOW_CLOSE_CANCELLED" });
  assert.deepEqual(racingBegins, ["1"]);
  assert.deepEqual(steps(racing.log), ["decide", "ask"]);
  asks[1].answer.resolve({ confirmed: true, suppressed: false });
  await current;
  assert.deepEqual(log.filter(([step]) => step === "begin"), [["begin", "2"]]);
});

test("Cancel rejects exactly that attempt without quiescing, and a rejection timeout never resumes", async () => {
  const timeout = harness({ reject: async (value) => { timeout.log.push(["reject", value.closeGeneration]); throw new Error("transport timeout"); } });
  const closing = timeout.coordinator.accept(main);
  await tick();
  timeout.asks[0].answer.resolve({ confirmed: false, suppressed: false });
  await assert.rejects(closing, /transport timeout/);
  assert.deepEqual(steps(timeout.log), ["decide", "ask", "reject"]);
  assert.deepEqual(timeout.log.at(-1), ["reject", "1"]);
  const resumed = [];
  const failingToken = Object.freeze({ settled: Promise.resolve() });
  const failing = harness({
    drain: async () => { throw new Error("save failed"); },
    barrierReject: async () => { throw new Error("rejection timed out"); },
    suspend: () => failingToken,
    resume: (value) => resumed.push(value),
  });
  const draining = failing.coordinator.accept(main);
  await tick();
  failing.asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await assert.rejects(draining, /rejection timed out/);
  assert.equal(steps(failing.log).includes("resume"), false);
  assert.deepEqual(resumed, []);
  failing.coordinator.cancelled(main);
  assert.deepEqual(steps(failing.log).filter((step) => step === "resume"), ["resume"]);
  assert.equal(resumed.length, 1);
  assert.equal(resumed[0], failingToken);
  failing.coordinator.cancelled(main);
  assert.equal(resumed.length, 1);
});

test("a refused drain start rejects the pending native attempt and never quiesces", async () => {
  const { log, asks, coordinator } = harness({ beginDrain: async () => { throw Object.assign(new Error("Invalid close drain"), { code: "ERR_WINDOW_CLOSE_STALE" }); } });
  const closing = coordinator.accept(main);
  await tick();
  asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await assert.rejects(closing, { code: "ERR_WINDOW_CLOSE_STALE" });
  assert.deepEqual(steps(log), ["decide", "ask", "reject"]);
});

test("utility windows skip the tab warning but keep the drain and save barrier", async () => {
  const { log, coordinator } = harness({ utility: true });
  await coordinator.accept(main);
  assert.deepEqual(steps(log), ["begin", "quiesce", "drain", "acknowledge"]);
});

test("dialog suspension waits after allow and before quiescence and resumes once only on confirmed cancellation", async () => {
  const settled = deferred();
  const resumed = [];
  const order = [];
  const outerToken = Object.freeze({ settled: settled.promise });
  const suspended = harness({ suspend: (value) => { order.push(["suspend", value.closeGeneration]); return outerToken; }, resume: (value) => resumed.push(value), acknowledge: () => new Promise(() => {}) });
  const closing = suspended.coordinator.accept(main);
  await tick();
  suspended.asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await tick();
  assert.deepEqual(steps(suspended.log), ["decide", "ask", "begin"]);
  assert.deepEqual(order, [["suspend", "1"]]);
  settled.resolve();
  await tick(); await tick();
  assert.deepEqual(steps(suspended.log), ["decide", "ask", "begin", "quiesce", "drain"]);
  suspended.coordinator.cancelled(main);
  suspended.coordinator.cancelled(main);
  assert.equal(resumed.length, 1);
  assert.equal(resumed[0], outerToken);
  assert.deepEqual(steps(suspended.log).filter((step) => step === "resume"), ["resume"]);
  void closing.catch(() => {});
  const early = deferred();
  const earlyToken = Object.freeze({ settled: early.promise });
  const beforeQuiesce = harness({ suspend: () => earlyToken, resume: (value) => resumed.push(value) });
  const earlyClose = beforeQuiesce.coordinator.accept(main);
  await tick();
  beforeQuiesce.asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await tick();
  beforeQuiesce.coordinator.cancelled(main);
  early.resolve();
  await assert.rejects(earlyClose, { code: "ERR_WINDOW_CLOSE_CANCELLED" });
  assert.equal(steps(beforeQuiesce.log).includes("quiesce"), false);
  assert.equal(resumed.length, 2);
  assert.equal(resumed[1], earlyToken);
  const done = harness({ suspend: () => ({ settled: Promise.resolve() }), resume: () => assert.fail("a completed close never resumes dialogs") });
  const completed = done.coordinator.accept(main);
  await tick();
  done.asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await completed;
});

test("a managed last-tab close carries its reason even when the native event arrives before the request reply", async () => {
  const reply = deferred();
  const { log, asks, coordinator } = harness({ request: () => reply.promise });
  const closing = coordinator.close("lastTab");
  const event = coordinator.accept(main);
  reply.resolve(main);
  await tick();
  assert.equal(asks.length, 1);
  assert.deepEqual(log[0], ["decide", "main", "lastTab"]);
  asks[0].answer.resolve({ confirmed: false, suppressed: true });
  await assert.rejects(closing, { code: "ERR_WINDOW_CLOSE_CANCELLED" });
  await assert.rejects(event, { code: "ERR_WINDOW_CLOSE_CANCELLED" });
  assert.deepEqual(steps(log), ["decide", "ask", "suppress", "reject", "flush"]);
  const next = coordinator.accept({ ...main, closeGeneration: "2" });
  await tick();
  assert.deepEqual(log.at(-2), ["decide", "main", "window"]);
  asks[1].answer.resolve({ confirmed: true, suppressed: false });
  await next;
  assert.equal(coordinator.accept({ ...main, windowId: "other" }) instanceof Promise, true);
});

test("a closing window departs the session only after the drain starts and only when the close does not end the session", async () => {
  const leaving = harness({ endsSession: false });
  const closing = leaving.coordinator.accept(main);
  await tick();
  leaving.asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await closing;
  assert.deepEqual(steps(leaving.log), ["decide", "ask", "begin", "depart", "flush", "quiesce", "drain", "acknowledge"]);
  const ending = harness({ endsSession: true });
  const quitting = ending.coordinator.accept(main);
  await tick();
  ending.asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await quitting;
  assert.equal(steps(ending.log).includes("depart"), false);
  const utility = harness({ endsSession: false, utility: true });
  await utility.coordinator.accept(main);
  assert.equal(steps(utility.log).includes("depart"), false);
});

test("a departed window stays recorded until cancellation is confirmed, then is restored exactly once", async () => {
  const ack = deferred();
  const vetoed = harness({ endsSession: false, acknowledge: () => ack.promise, barrierReject: async () => { throw new Error("rejection timed out"); } });
  const closing = vetoed.coordinator.accept(main);
  await tick();
  vetoed.asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await tick(); await tick();
  ack.reject(new Error("transport timeout"));
  await assert.rejects(closing, /rejection timed out/);
  assert.equal(steps(vetoed.log).includes("stay"), false);
  vetoed.coordinator.cancelled(main);
  vetoed.coordinator.cancelled(main);
  vetoed.coordinator.producersResumed(main);
  assert.deepEqual(vetoed.log.filter(([step]) => step === "stay"), [["stay", "1"]]);
  const failing = harness({ endsSession: false, flush: async () => { throw new Error("Save rejected"); } });
  const failed = failing.coordinator.accept(main);
  await tick();
  failing.asks[0].answer.resolve({ confirmed: true, suppressed: false });
  await assert.rejects(failed, /Save rejected/);
  assert.deepEqual(steps(failing.log), ["decide", "ask", "begin", "depart", "reject", "stay"]);
});

const { createHostDialogs } = await import("./hostDialogs.ts");

function adapterHarness() {
  const listeners = new Set();
  const requests = [];
  let sequence = 0;
  const host = {
    bootstrap: { windowId: "main", profileId: "default" },
    onEvent(listener) { listeners.add(listener); return () => listeners.delete(listener); },
    onBeforeClose() { return () => {}; },
    async invoke(method, data) {
      requests.push({ method, ...data });
      return method === "ArdiaShell.windowIdentity" ? { windowId: "main", rendererGeneration: "9007199254740993" } : { accepted: true };
    },
  };
  const dialogs = createHostDialogs(host, () => `dialog:close-warning-${++sequence}`);
  const resolve = (result) => {
    const begun = requests.findLast((r) => r.method === "ArdiaShell.beginDialog");
    for (const listener of listeners) listener("ArdiaShell", "shell", { name: "shell.dialogResolved", data: { dialogId: begun.args[0].dialogId, windowId: "main", rendererGeneration: "9007199254740993", result } });
  };
  const log = [];
  const barrier = createWindowCloseBarrier({
    quiesce: () => log.push("quiesce"),
    drain: async () => { log.push("drain"); return 0; },
    acknowledge: () => new Promise(() => {}),
    reject: async () => {},
    resume: (value) => { log.push("resume"); coordinator.producersResumed(value); },
  });
  const coordinator = createWindowCloseCoordinator({
    windowId: "main",
    utility: false,
    decide: (windowId, _reason, ask) => ask({ title: "Close 2 tabs?", suppression: "Don’t ask me again", windowId })
      .then(({ confirmed, suppressed }) => ({ decision: confirmed ? "allow" : "cancel", suppressWarning: suppressed })),
    ask: (options, signal) => dialogs.abortableConfirm(options, signal),
    suppress: () => log.push("suppress"),
    flush: async () => {},
    request: async () => main,
    beginDrain: async () => { log.push("begin"); return { accepted: true, endsSession: true }; },
    reject: async () => { log.push("reject"); },
    barrier,
    cancelled: barrier.cancelled,
    suspendDialogs: () => dialogs.suspend(),
    resumeDialogs: (suspension) => dialogs.resume(suspension),
  });
  return { dialogs, requests, resolve, log, coordinator };
}

test("with the real dialog adapter, native cancellation cancels the open warning natively and ignores its late answer", async () => {
  const f = adapterHarness();
  const closing = f.coordinator.accept(main);
  await tick(); await tick();
  const begun = f.requests.findLast((r) => r.method === "ArdiaShell.beginDialog");
  assert.ok(begun);
  f.coordinator.cancelled(main);
  await assert.rejects(closing, { code: "ERR_WINDOW_CLOSE_CANCELLED" });
  assert.deepEqual(f.requests.findLast((r) => r.method === "ArdiaShell.cancelDialog")?.args, [begun.args[0].dialogId]);
  f.resolve({ confirmed: true, suppressed: true });
  await tick();
  assert.deepEqual(f.log, []);
  f.dialogs.retire();
});

test("with the real dialog adapter, allow suspends dialogs for the drain and confirmed cancellation releases only this attempt's token", async () => {
  const f = adapterHarness();
  const outer = f.dialogs.suspend();
  assert.equal(f.dialogs.resume(outer), true);
  const closing = f.coordinator.accept(main);
  await tick(); await tick();
  f.resolve({ confirmed: true, suppressed: false });
  await tick(); await tick(); await tick();
  assert.deepEqual(f.log, ["begin", "quiesce", "drain"]);
  await assert.rejects(f.dialogs.confirm({ title: "During drain" }), /suspended/);
  const unrelated = f.dialogs.suspend();
  f.coordinator.cancelled(main);
  void closing.catch(() => {});
  assert.deepEqual(f.log, ["begin", "quiesce", "drain", "resume"]);
  await assert.rejects(f.dialogs.confirm({ title: "Still held by another suspension" }), /suspended/);
  assert.equal(f.dialogs.resume(unrelated), true);
  const after = f.dialogs.confirm({ title: "After cancellation" });
  await tick(); await tick();
  f.resolve({ confirmed: true, suppressed: false });
  assert.deepEqual(await after, { confirmed: true, suppressed: false });
  f.dialogs.retire();
});
