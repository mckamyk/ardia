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
