import assert from "node:assert/strict";
import test from "node:test";
import { create } from "zustand";
import { followInstallRequest } from "./installRequestSheet.ts";

const request = (windowId, name = "Fixture") => ({ windowId, profile: "", source: "unpacked", status: "ready", pkg: { name, path: `/tmp/${name}` } });

function sheet(windowId = "settings", install = null) {
  const store = create(() => ({ install }));
  const closed = [];
  const stop = followInstallRequest(store, windowId, () => closed.push(store.getState().install));
  return { store, closed, stop };
}

test("a sheet opened before the chosen folder is read stays open until the request arrives and is answered", () => {
  const f = sheet();
  assert.deepEqual(f.closed, []);
  f.store.setState({ install: request("settings") });
  assert.deepEqual(f.closed, []);
  f.store.setState({ install: { ...request("settings"), status: "installing" } });
  assert.deepEqual(f.closed, []);
  f.store.setState({ install: null });
  assert.deepEqual(f.closed, [null]);
  f.stop();
});

test("every Choose Folder… in a row gets its own dialog, and each answer closes the sheet once", () => {
  for (const name of ["first", "second", "third"]) {
    const f = sheet();
    assert.deepEqual(f.closed, [], `${name}: closed before the folder came back`);
    f.store.setState({ install: request("settings", name) });
    f.store.setState({ install: null });
    f.store.setState({ install: null });
    assert.equal(f.closed.length, 1, name);
    f.stop();
  }
});

test("a request that arrived before the sheet subscribed is followed and closes the sheet when answered", () => {
  const f = sheet("settings", request("settings"));
  f.store.setState({ install: null });
  assert.deepEqual(f.closed, [null]);
  f.stop();
});

test("a request for another window neither opens nor closes the Settings sheet", () => {
  const f = sheet();
  f.store.setState({ install: request("main") });
  f.store.setState({ install: null });
  assert.deepEqual(f.closed, []);
  f.store.setState({ install: request("settings") });
  f.store.setState({ install: request("main") });
  assert.equal(f.closed.length, 1);
  f.stop();
});

test("an unmounted sheet stops following", () => {
  const f = sheet();
  f.store.setState({ install: request("settings") });
  f.stop();
  f.store.setState({ install: null });
  assert.deepEqual(f.closed, []);
});
