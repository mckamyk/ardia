import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { readFileSync } from "node:fs";
import test from "node:test";
import { runInNewContext } from "node:vm";
import { HostTab } from "./hostTab.ts";

const bootstrap = readFileSync(new URL("../../../engine/ardiacore-linux/src/ardia/linux/bootstrap.js", import.meta.url), "utf8");

function fixture() {
  const requests = [];
  const mounts = new Map();
  let nextTab = 6;
  let refusal;
  const context = {
    location: { protocol: "chrome:", hostname: "ardia" },
    crypto: { randomUUID },
    __ardiaBootstrap: { windowId: "commands", profileId: "default" },
    setTimeout, clearTimeout, addEventListener() {}, console,
    chrome: { send(name, [requestId, operation, payload]) {
      assert.equal(name, "ardiaRequest");
      assert.equal(operation, "host.invoke");
      const { method, args } = payload;
      requests.push({ method, args });
      if (method === "tab.create") {
        const previous = mounts.get(args.key);
        const id = previous?.id ?? ++nextTab;
        mounts.set(args.key, { id, claim: args.claim });
        context.__ardiaReply(requestId, true, { browserId: id, chromeTabId: id });
      } else if (refusal) context.__ardiaReply(requestId, false, refusal);
      else context.__ardiaReply(requestId, true, null);
    } },
  };
  context.window = context;
  context.top = context;
  runInNewContext(bootstrap, context);
  return {
    host: context.__ardiaHost, requests,
    refuse(error) { refusal = error; },
    emit(id, value) { context.__ardiaEvent("ArdiaWebView", String(id), { type: "findResult", value }); },
  };
}

const settle = () => new Promise(setImmediate);

test("page command contracts retain the exact pane, claim and arguments through the production transport", async () => {
  const f = fixture();
  const a = new HostTab(f.host, { key: "left" }, () => {});
  const b = new HostTab(f.host, { key: "right" }, () => {});
  await Promise.all([a.ready, b.ready]);
  try {
    const commands = [
      ["find", ["needle", true, false]], ["find", ["needle", true, true]],
      ["find", ["needle", false, true]], ["find", ["", true, false]],
      ["stopFinding", [false]], ["stopFinding", [true]], ["print", []],
      ["runPageCommand", ["savePage"]], ["runPageCommand", ["systemPrint"]],
      ["showDevTools", []], ["showDevTools", ["toggle"]],
      ["showDevTools", ["inspect"]], ["showDevTools", ["console"]],
    ];
    for (const [command, args] of commands) {
      assert.equal(await b.command(command, args), null);
      const request = f.requests.at(-1);
      assert.deepEqual(request, { method: "tab.command", args: { id: b.identity.browserId, claim: b.claim, command, args } });
      assert.notEqual(request.args.id, a.identity.browserId);
    }
  } finally {
    a.dispose(); b.dispose(); await settle();
  }
});

test("Find acknowledgements carry no result; separate events reach only their pane and current mount", async () => {
  const f = fixture();
  const left = [], old = [], replacement = [];
  const a = new HostTab(f.host, { key: "left" }, (event) => left.push(event));
  const b = new HostTab(f.host, { key: "right" }, (event) => old.push(event));
  await Promise.all([a.ready, b.ready]);
  assert.equal(await b.command("find", ["needle", true, false]), null);
  const result = { count: 4, active: 2, final: true };
  f.emit(b.identity.browserId, result);
  assert.deepEqual(old.filter((event) => event.type === "findResult"), [{ type: "findResult", value: result }]);
  assert.equal(left.filter((event) => event.type === "findResult").length, 0);
  const c = new HostTab(f.host, { key: "right" }, (event) => replacement.push(event));
  await c.ready;
  assert.equal(c.identity.browserId, b.identity.browserId);
  assert.notEqual(c.claim, b.claim);
  f.emit(c.identity.browserId, { count: 2, active: 1, final: false });
  assert.equal(old.filter((event) => event.type === "findResult").length, 1);
  assert.equal(replacement.filter((event) => event.type === "findResult").length, 1);
  await assert.rejects(b.command("find", ["stale", true, false]), /released/);
  a.dispose(); b.dispose(); c.dispose(); await settle();
  f.emit(c.identity.browserId, result);
  assert.equal(replacement.filter((event) => event.type === "findResult").length, 1);
});

test("structured refusals reject with stable codes without changing target or retrying", async () => {
  const f = fixture();
  const tab = new HostTab(f.host, { key: "target" }, () => {});
  await tab.ready;
  for (const [command, args, code] of [
    ["runPageCommand", ["caretBrowsing"], "ERR_PAGE_UNSUPPORTED"],
    ["print", [], "ERR_PAGE_POLICY"], ["showDevTools", [], "ERR_PAGE_UNAVAILABLE"],
    ["runPageCommand", ["savePage"], "ERR_PAGE_STALE_TARGET"],
  ]) {
    f.refuse({ code, message: "Page command refused" });
    const before = f.requests.length;
    await assert.rejects(tab.command(command, args), { code, message: "Page command refused" });
    assert.equal(f.requests.length, before + 1);
    assert.equal(f.requests.at(-1).args.id, tab.identity.browserId);
  }
  f.refuse(undefined);
  tab.dispose(); await settle();
});
