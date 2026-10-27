import assert from "node:assert/strict";
import test from "node:test";
import { mergeDownloadSnapshot, nativeDownloadId, snapshotDownload, startHostDownloads } from "./hostDownloads.ts";

const ID = (n) => `3f2504e0-4f89-41d3-9a0c-${String(n).padStart(12, "0")}`;
const download = (n, patch = {}) => ({
  id: ID(n), url: `https://example.test/${n}`, filename: `${n}.zip`, path: `/home/me/Downloads/${n}.zip`, state: "finished",
  paused: false, received: 10, total: 10, speed: 0, mimeType: "application/zip", ...patch,
});

function fixture(list = []) {
  const listeners = new Set();
  const calls = [];
  const replies = new Map([["downloads.list", JSON.stringify(list)]]);
  const host = {
    bootstrap: { windowId: "main", profileId: "default", capabilities: [] },
    call: async (name, profile, args) => {
      calls.push({ name, profile, args: JSON.parse(args) });
      const reply = replies.get(name);
      if (reply instanceof Error) throw reply;
      return reply ?? "null";
    },
    invoke: async () => null,
    onEvent: (listener) => { listeners.add(listener); return () => listeners.delete(listener); },
  };
  const state = {
    downloads: [],
    removeDownload(id) { state.downloads = state.downloads.filter((d) => d.id !== id); },
    clearDownloads() { state.downloads = state.downloads.filter((d) => d.state === "downloading"); },
  };
  const store = { getState: () => state, setState: (patch) => Object.assign(state, patch) };
  const emit = (module, event, payload) => { for (const listener of [...listeners]) listener(module, event, payload); };
  return { host, store, state, calls, replies, emit };
}

const settle = () => new Promise((resolve) => setTimeout(resolve, 0));

test("only Chromium download GUIDs are native download ids", () => {
  assert.equal(nativeDownloadId(ID(1)), true);
  assert.equal(nativeDownloadId(ID(1).toUpperCase()), true);
  for (const id of ["saved-0", "", `${ID(1)}x`, "wk-download-1", 7, null]) assert.equal(nativeDownloadId(id), false);
});

test("snapshot entries are validated, never off the record and always the regular profile", () => {
  assert.deepEqual(snapshotDownload({ ...download(1), profile: "/home/me/.config/ardia/Default" }), { ...download(1), profile: "" });
  for (const bad of [null, "x", { ...download(1), id: "saved-1" }, { ...download(1), state: "paused" }, { ...download(1), offTheRecord: true },
    { ...download(1), received: "10" }, { ...download(1), path: null }, { ...download(1), paused: "no" }]) {
    assert.equal(snapshotDownload(bad), null);
  }
});

test("the startup snapshot fills the list once, newest first, after live entries and within the cap", () => {
  const live = download(9, { state: "downloading", received: 1 });
  const merged = mergeDownloadSnapshot([live], [download(9), download(3), download(2), { bad: true }, download(3)]);
  assert.deepEqual(merged.map((d) => d.id), [ID(9), ID(3), ID(2)]);
  assert.equal(merged[0], live);
  assert.equal(mergeDownloadSnapshot(merged, [download(3)]), null);
  assert.equal(mergeDownloadSnapshot([], { downloads: [] }), null);
  const many = Array.from({ length: 120 }, (_, n) => download(n));
  assert.equal(mergeDownloadSnapshot([], many).length, 100);
  assert.equal(mergeDownloadSnapshot(many.slice(0, 100), [download(500)]), null);
});

test("startup lists downloads through the regular profile without opening the popover path", async () => {
  const f = fixture([download(2), download(1)]);
  const running = startHostDownloads(f.host, f.store);
  await running.ready;
  assert.deepEqual(f.calls, [{ name: "downloads.list", profile: "default", args: {} }]);
  assert.deepEqual(f.state.downloads.map((d) => d.id), [ID(2), ID(1)]);
  running.stop();
});

test("removing and clearing finished downloads removes them from Chromium history so restart cannot resurrect them", async () => {
  const f = fixture();
  const running = startHostDownloads(f.host, f.store);
  await running.ready;
  f.state.downloads = [download(1), download(2, { state: "downloading" }), download(3, { state: "cancelled" }), { ...download(4), id: "saved-4" }];
  f.state.removeDownload(ID(1));
  f.state.clearDownloads("main");
  await settle();
  assert.deepEqual(f.state.downloads.map((d) => d.id), [ID(2)]);
  assert.deepEqual(f.calls.slice(1), [
    { name: "downloads.remove", profile: "default", args: { id: ID(1) } },
    { name: "downloads.remove", profile: "default", args: { id: ID(3) } },
  ]);
  running.stop();
});

test("a resumable failed download is cancelled before it is removed", async () => {
  const f = fixture();
  const running = startHostDownloads(f.host, f.store);
  await running.ready;
  let removes = 0;
  f.host.call = async (name, profile, args) => {
    f.calls.push({ name, profile, args: JSON.parse(args) });
    if (name === "downloads.remove" && removes++ === 0) throw Object.assign(new Error("download in progress"), { code: "ERR_ENGINE" });
    return "null";
  };
  f.state.downloads = [download(5, { state: "failed" })];
  f.state.removeDownload(ID(5));
  await settle(); await settle(); await settle();
  assert.deepEqual(f.calls.slice(1).map((c) => c.name), ["downloads.remove", "downloads.cancel", "downloads.remove"]);
  running.stop();
});

test("removal events from another window update this list without echoing a native remove", async () => {
  const f = fixture([download(1), download(2)]);
  const running = startHostDownloads(f.host, f.store);
  await running.ready;
  f.emit("ArdiaDownloads", "downloads", { name: "downloads.removed", data: { id: ID(1), profile: "" } });
  f.emit("ArdiaDownloads", "downloads", { name: "downloads.removed", data: { id: "saved-2" } });
  f.emit("ArdiaTabs", "downloads", { name: "downloads.removed", data: { id: ID(2) } });
  f.emit("ArdiaDownloads", "downloads", { name: "downloads.changed", data: { id: ID(2) } });
  await settle();
  assert.deepEqual(f.state.downloads.map((d) => d.id), [ID(2)]);
  assert.equal(f.calls.filter((c) => c.name === "downloads.remove").length, 0);
  running.stop();
});

test("a failed snapshot is reported and leaves live entries alone; stop restores the shared actions", async () => {
  const f = fixture();
  const { removeDownload, clearDownloads } = f.state;
  f.replies.set("downloads.list", Object.assign(new Error("This call is unavailable on Linux"), { code: "ERR_UNKNOWN_CALL" }));
  const errors = [];
  const original = console.error;
  console.error = (...args) => errors.push(args);
  try {
    f.state.downloads = [download(7, { state: "downloading" })];
    const running = startHostDownloads(f.host, f.store);
    await running.ready;
    assert.equal(errors.length, 1);
    assert.deepEqual(f.state.downloads.map((d) => d.id), [ID(7)]);
    running.stop();
    assert.equal(f.state.removeDownload, removeDownload);
    assert.equal(f.state.clearDownloads, clearDownloads);
  } finally {
    console.error = original;
  }
});
