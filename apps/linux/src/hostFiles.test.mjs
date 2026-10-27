import assert from "node:assert/strict";
import test from "node:test";
import { createHostFiles, FILE_STATUS_FRESH_MS } from "./hostFiles.ts";
import { resolveHostModule } from "./hostModules.ts";

function fixture(capabilities = ["ArdiaSystem.fileStatus"]) {
  const invokes = [];
  const handlers = new Map();
  const host = {
    bootstrap: { windowId: "main", profileId: "default", capabilities },
    call: async () => "null",
    invoke: async (method, args) => {
      invokes.push({ method, args });
      const handler = handlers.get(method);
      return handler ? handler(...args.args) : null;
    },
    onEvent: () => () => {},
  };
  let time = 1000;
  const timers = [];
  const clock = { now: () => time, schedule: (run) => timers.push(run) };
  const flush = async () => {
    while (timers.length) timers.shift()();
    await new Promise((resolve) => setTimeout(resolve, 0));
  };
  return { host, invokes, handlers, clock, flush, advance: (ms) => (time += ms) };
}

const quiet = async (run) => {
  const original = console.error;
  const errors = [];
  console.error = (...args) => errors.push(args);
  try { await run(); } finally { console.error = original; }
  return errors;
};

test("ArdiaSystem.fileExists is synchronous and boolean instead of a truthy host promise", async () => {
  const { host, handlers, invokes } = fixture();
  handlers.set("ArdiaSystem.fileStatus", (paths) => paths.map(() => true));
  const exists = resolveHostModule(host, "ArdiaSystem", "fileExists");
  assert.equal(exists("/home/me/Downloads/a.zip"), false);
  await new Promise((resolve) => setTimeout(resolve, 50));
  assert.deepEqual(invokes, [{ method: "ArdiaSystem.fileStatus", args: { args: [["/home/me/Downloads/a.zip"]] } }]);
  assert.equal(exists("/home/me/Downloads/a.zip"), true);
  for (const method of ["openFile", "revealFile", "moveToTrash", "fileIcon", "appIcon"]) assert.equal(typeof resolveHostModule(host, "ArdiaSystem", method), "function");
  assert.equal(resolveHostModule(host, "ArdiaSystem", "fileExists"), exists);
});

test("file existence is batched, cached, refreshed after it goes stale and announces changes", async () => {
  const f = fixture();
  const present = new Set(["/d/a.zip"]);
  f.handlers.set("ArdiaSystem.fileStatus", (paths) => paths.map((path) => present.has(path)));
  const files = createHostFiles(f.host, f.clock);
  let changes = 0;
  files.onChange(() => changes++);
  assert.equal(files.fileExists("/d/a.zip"), false);
  assert.equal(files.fileExists("/d/b.zip"), false);
  assert.equal(files.fileExists("/d/a.zip"), false);
  await f.flush();
  assert.deepEqual(f.invokes, [{ method: "ArdiaSystem.fileStatus", args: { args: [["/d/a.zip", "/d/b.zip"]] } }]);
  assert.equal(changes, 1);
  assert.equal(files.fileExists("/d/a.zip"), true);
  assert.equal(files.fileExists("/d/b.zip"), false);
  await f.flush();
  assert.equal(f.invokes.length, 1);
  present.delete("/d/a.zip");
  f.advance(FILE_STATUS_FRESH_MS + 1);
  assert.equal(files.fileExists("/d/a.zip"), true);
  await f.flush();
  assert.equal(files.fileExists("/d/a.zip"), false);
  assert.equal(changes, 2);
});

test("relative, oversized and NUL paths never reach the host and old hosts report false", async () => {
  const f = fixture();
  const files = createHostFiles(f.host, f.clock);
  for (const path of ["relative.zip", "", "/d/\0x", `/${"a".repeat(5000)}`, null]) assert.equal(files.fileExists(path), false);
  await f.flush();
  assert.equal(f.invokes.length, 0);
  const old = fixture([]);
  assert.equal(createHostFiles(old.host, old.clock).fileExists("/d/a.zip"), false);
  await old.flush();
  assert.equal(old.invokes.length, 0);
});

test("large batches are split and a failed status keeps the last answer", async () => {
  const f = fixture();
  let fail = true;
  f.handlers.set("ArdiaSystem.fileStatus", (paths) => {
    if (fail) throw Object.assign(new Error("boom"), { code: "ERR_ENGINE" });
    return paths.map(() => true);
  });
  const files = createHostFiles(f.host, f.clock);
  const errors = await quiet(async () => {
    for (let n = 0; n < 70; n++) files.fileExists(`/d/${n}`);
    await f.flush();
  });
  assert.equal(errors.length, 2);
  assert.deepEqual(f.invokes.map((i) => i.args.args[0].length), [64, 6]);
  assert.equal(files.fileExists("/d/1"), false);
  fail = false;
  f.advance(FILE_STATUS_FRESH_MS + 1);
  files.fileExists("/d/1");
  await f.flush();
  assert.equal(files.fileExists("/d/1"), true);
});

test("open, reveal and trash return booleans, report native errors and trash clears existence", async () => {
  const f = fixture();
  f.handlers.set("ArdiaSystem.fileStatus", (paths) => paths.map(() => true));
  f.handlers.set("ArdiaSystem.openFile", () => true);
  f.handlers.set("ArdiaSystem.revealFile", () => { throw Object.assign(new Error("No desktop file manager or xdg-open is available"), { code: "ERR_REVEAL_UNAVAILABLE" }); });
  f.handlers.set("ArdiaSystem.moveToTrash", () => true);
  const files = createHostFiles(f.host, f.clock);
  files.fileExists("/d/a.zip");
  await f.flush();
  assert.equal(files.fileExists("/d/a.zip"), true);
  assert.equal(await files.openFile("/d/a.zip"), true);
  const errors = await quiet(async () => assert.equal(await files.revealFile("/d/a.zip"), false));
  assert.equal(errors[0][1].code, "ERR_REVEAL_UNAVAILABLE");
  let changes = 0;
  files.onChange(() => changes++);
  assert.equal(await files.moveToTrash("/d/a.zip"), true);
  assert.equal(files.fileExists("/d/a.zip"), false);
  assert.equal(changes, 1);
  assert.equal(await files.openFile("relative"), false);
  assert.deepEqual(f.invokes.map((i) => i.method), ["ArdiaSystem.fileStatus", "ArdiaSystem.openFile", "ArdiaSystem.revealFile", "ArdiaSystem.moveToTrash"]);
});

test("file icons accept only PNG data URLs and fall back to null", async () => {
  const f = fixture();
  const files = createHostFiles(f.host, f.clock);
  f.handlers.set("ArdiaSystem.fileIcon", () => "data:image/png;base64,iVBORw0KGgo=");
  assert.equal(await files.fileIcon("report.pdf", 28), "data:image/png;base64,iVBORw0KGgo=");
  f.handlers.set("ArdiaSystem.fileIcon", () => "javascript:alert(1)");
  assert.equal(await files.fileIcon("report.pdf", 28), null);
  f.handlers.set("ArdiaSystem.fileIcon", () => { throw new Error("no icon"); });
  assert.equal(await files.fileIcon("report.pdf", 28), null);
  assert.equal(await files.fileIcon("", 28), null);
  assert.equal(await files.appIcon(42, 32), null);
});
