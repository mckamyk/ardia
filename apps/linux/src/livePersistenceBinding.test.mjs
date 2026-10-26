import assert from "node:assert/strict";
import test from "node:test";
import { registerHooks } from "node:module";
import { projectLiveIntent } from "./liveDocumentIntent.ts";

const platform = new URL("../../browser/src/platform/livePersistence.ts", import.meta.url).href;
registerHooks({
  resolve(specifier, context, next) {
    const resolved = next(specifier, context);
    return resolved.url === platform ? { url: new URL("./livePersistence.ts", import.meta.url).href, shortCircuit: true } : resolved;
  },
});
const listeners = new Set();
let payload = { folders: {}, folderOrder: [], items: {}, known: {}, unread: {}, hidden: {}, completed: {}, expandedStacks: {}, accounts: {}, config: {}, calendar: { showPreview: true, showTimeToNext: true, hiddenCalendarIds: [] }, meetingGroups: {}, dismissedAlerts: [], version: 1 };
let revision = 0, watermark = 0;
const snapshot = () => ({ document: "live.json", scope: "native", epoch: "epoch", clientId: "native-client", revision, watermark, payload, incarnations: Object.fromEntries(Object.keys(payload.folders).map((id) => [id, `folder:${id}`])), generations: {} });
globalThis.window = { __ardiaHost: {
  bootstrap: { windowId: "main", profileId: "default" },
  onEvent(listener) { listeners.add(listener); return () => listeners.delete(listener); },
  async call(name, profile, json) {
    const args = JSON.parse(json);
    if (name === "documents.watch") return JSON.stringify(snapshot());
    if (name === "documents.flush") return JSON.stringify(snapshot());
    assert.equal(name, "documents.commit");
    payload = projectLiveIntent(payload, args.operations);
    revision++; watermark++;
    return JSON.stringify({ status: "committed", requestId: args.requestId, snapshot: snapshot() });
  },
} };
const { live, setLive, createFolder, updateCalendarSettings } = await import("../../browser/src/state/live/store.ts");
const { flushLinuxDocuments } = await import("./documentFlush.ts");

test("Linux binding uses the canonical shared store, suppresses write echoes and preserves transient state", async () => {
  await new Promise((resolve) => setImmediate(resolve));
  setLive({ status: { f: { state: "updating" } }, completing: { f: ["item"] }, showAll: { f: true } });
  payload = { ...payload, calendar: { ...payload.calendar, showPreview: false } };
  revision++;
  for (const listener of listeners) listener("ArdiaDocuments", "documents", { name: "documents.changed", data: { snapshot: snapshot(), clientId: "peer", requestId: "peer:1" } });
  assert.equal(live().calendar.showPreview, false);
  assert.equal(live().status.f.state, "updating");
  assert.deepEqual(live().completing.f, ["item"]);
  assert.equal(live().showAll.f, true);
  assert.equal(watermark, 0);
  updateCalendarSettings({ showTimeToNext: false });
  await flushLinuxDocuments();
  assert.equal(watermark, 1);
  assert.equal(payload.calendar.showTimeToNext, false);
  const oldNow = Date.now;
  Date.now = () => 1;
  try {
    const ids = Array.from({ length: 30 }, () => createFolder("logical", "documents", []));
    assert.equal(new Set(ids).size, ids.length);
    assert.ok(ids.every((id) => /^lf-[a-f0-9-]{36}$/.test(id)));
    await flushLinuxDocuments();
  } finally { Date.now = oldNow; }
});
