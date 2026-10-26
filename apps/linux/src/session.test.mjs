import assert from "node:assert/strict";
import test from "node:test";
import { createUtilitySessionSnapshot, initialLinuxSession } from "./session.ts";
import { startHostPersistence } from "../../demo/src/hostSession.ts";
import { useBrowser } from "../../browser/src/store/browser.ts";
import { mergeSession } from "../../demo/src/hostSessionMerge.ts";

const host = (windowId = "main") => ({ bootstrap: { windowId, profileId: "default", startup: false } });

function browserSession() {
  const saved = initialLinuxSession(host(), null);
  saved.settings = { restoreSession: false, sidebarWidth: 256 };
  const tab = Object.values(saved.tabs)[0];
  tab.url = "https://example.test/playing";
  tab.title = "Playing page";
  tab.lastActiveAt = 123;
  return saved;
}

test("utility startup retains browser windows and active page without inventing utility tabs or applying launch restore policy", () => {
  const saved = browserSession();
  for (const id of ["settings", "import"]) {
    const initial = initialLinuxSession(host(id), saved);
    assert.equal(initial, saved);
    useBrowser.getState().hydrate(initial);
    assert.deepEqual(useBrowser.getState().windowOrder, ["main"]);
    assert.equal(useBrowser.getState().windows[id], undefined);
    assert.equal(Object.values(useBrowser.getState().tabs).length, 1);
    assert.equal(Object.values(useBrowser.getState().tabs)[0].url, "https://example.test/playing");
  }
  assert.deepEqual(initialLinuxSession(host("settings"), null), {});
});

test("utility snapshots preserve canonical browser geometry, pages, groups and focus while saving live profile names and settings", () => {
  const saved = browserSession();
  useBrowser.getState().hydrate(saved);
  const snapshotUtilitySession = createUtilitySessionSnapshot(useBrowser.getState(), saved);
  useBrowser.getState().updateSettings({ sidebarWidth: 310 });
  useBrowser.setState((state) => ({ profiles: { ...state.profiles, default: { ...state.profiles.default, name: "Renamed Personal" } } }));
  const snapshot = snapshotUtilitySession(useBrowser.getState(), saved);
  for (const field of ["windows", "windowOrder", "tabs", "groups", "splits", "focusedWindowId", "lastProfileId"]) assert.deepEqual(snapshot[field], saved[field]);
  assert.equal(snapshot.settings.sidebarWidth, 310);
  assert.equal(snapshot.profiles.default.name, "Renamed Personal");
  assert.equal(Object.values(snapshot.tabs)[0].lastActiveAt, 123);
});

test("utility persistence rebases settings edits on canonical browser navigation without echoes or phantom native windows", async () => {
  const savedWindow = globalThis.window;
  const savedDocument = globalThis.document;
  globalThis.window = { addEventListener() {}, removeEventListener() {} };
  globalThis.document = { hidden: false, addEventListener() {}, removeEventListener() {} };
  const initial = browserSession();
  let canonical = initial;
  const writes = [];
  const listeners = new Set();
  let finalize;
  const native = {
    ...host("settings"),
    async invoke(method, pair) {
      assert.equal(method, "session.write");
      writes.push(pair);
      canonical = mergeSession(pair.base, pair.session, canonical);
      return canonical;
    },
    onEvent(listener) { listeners.add(listener); return () => listeners.delete(listener); },
    onBeforeClose(listener) { finalize = listener; return () => {}; },
  };
  useBrowser.getState().hydrate(initial);
  const stop = startHostPersistence(native, useBrowser, initial, createUtilitySessionSnapshot(useBrowser.getState(), initial));
  try {
    await new Promise((resolve) => setTimeout(resolve, 350));
    const before = canonical;
    const tabId = Object.keys(canonical.tabs)[0];
    canonical = { ...canonical, tabs: { ...canonical.tabs, [tabId]: { ...canonical.tabs[tabId], url: "https://example.test/next" } } };
    for (const listener of listeners) listener("ArdiaHost", "session", { base: before, session: canonical });
    const next = canonical;
    canonical = { ...canonical, tabs: { ...canonical.tabs, [tabId]: { ...canonical.tabs[tabId], url: "https://example.test/final" } } };
    for (const listener of listeners) listener("ArdiaHost", "session", { base: next, session: canonical });
    useBrowser.getState().updateSettings({ sidebarWidth: 330 });
    await new Promise((resolve) => setTimeout(resolve, 350));
    assert.equal(canonical.settings.sidebarWidth, 330);
    assert.equal(canonical.tabs[tabId].url, "https://example.test/final");
    assert.equal(canonical.tabs[tabId].lastActiveAt, 123);
    assert.deepEqual(Object.keys(canonical.windows), ["main"]);
    const count = writes.length;
    await new Promise((resolve) => setTimeout(resolve, 350));
    assert.equal(writes.length, count);
    finalize();
  } finally {
    stop();
    globalThis.window = savedWindow;
    globalThis.document = savedDocument;
  }
});

test("utility readiness recovers navigation requested while loading and never replaces a newer live request with an older reply", async () => {
  const { startUtilityNavigation } = await import("./utilityNavigation.ts");
  for (const liveRequest of [false, true]) {
    const listeners = new Set();
    let reply;
    let requested;
    const places = [];
    const initial = { pane: "general", profileId: null, detail: null };
    const pending = { pane: "profiles", profileId: "work", detail: null };
    const latest = { pane: "appearance", profileId: null, detail: null };
    const native = {
      bootstrap: { ...host("settings").bootstrap, utility: initial },
      onEvent(listener) { listeners.add(listener); return () => listeners.delete(listener); },
      invoke(method, args) {
        requested = [method, args];
        assert.equal(listeners.size, 1);
        return new Promise((resolve) => { reply = resolve; });
      },
    };
    const watcher = startUtilityNavigation(native, (place) => places.push(place));
    assert.deepEqual(requested, ["ArdiaShell.utilityReady", { args: ["settings"] }]);
    if (liveRequest) for (const listener of listeners) listener("ArdiaShell", "onUtilityNavigation", latest);
    reply(pending);
    await watcher.ready;
    assert.deepEqual(places.at(-1), liveRequest ? latest : pending);
    watcher.stop();
    assert.equal(listeners.size, 0);
  }
});

test("utility snapshots keep profile deletion and browser tabs created by Settings atomic", () => {
  const initial = browserSession();
  useBrowser.getState().hydrate(initial);
  const other = useBrowser.getState().createProfile({ name: "Work" });
  const saved = { ...initial, profiles: useBrowser.getState().profiles, profileOrder: useBrowser.getState().profileOrder };
  const snapshot = createUtilitySessionSnapshot(useBrowser.getState(), saved);
  useBrowser.getState().deleteProfile("default");
  const deleted = snapshot(useBrowser.getState(), saved);
  assert.equal(deleted.profiles.default, undefined);
  assert.equal(deleted.windows.main.profileId, other);
  assert.ok(Object.values(deleted.tabs).every((tab) => tab.profileId !== "default"));
  const afterDelete = createUtilitySessionSnapshot(useBrowser.getState(), deleted);
  const created = useBrowser.getState().newTab("main", { url: "https://chromewebstore.google.com/" });
  const opened = afterDelete(useBrowser.getState(), deleted);
  assert.equal(opened.tabs[created].url, "https://chromewebstore.google.com/");
  assert.ok(opened.windows.main.tabIds.includes(created));
});

test("a utility opened before the first browser save publishes no defaults and close-immediate saves only its edit", async () => {
  const savedWindow = globalThis.window;
  const savedDocument = globalThis.document;
  globalThis.window = { addEventListener() {}, removeEventListener() {} };
  globalThis.document = { hidden: false, addEventListener() {}, removeEventListener() {} };
  let canonical = {};
  const writes = [];
  const listeners = new Set();
  let final;
  const native = {
    ...host("settings"),
    async invoke(method, pair) {
      assert.equal(method, "session.write");
      writes.push(pair);
      canonical = mergeSession(pair.base, pair.session, canonical);
      return canonical;
    },
    onEvent(listener) { listeners.add(listener); return () => listeners.delete(listener); },
    onBeforeClose(listener) { final = listener; return () => {}; },
  };
  useBrowser.getState().hydrate({});
  const stop = startHostPersistence(native, useBrowser, {}, createUtilitySessionSnapshot(useBrowser.getState(), {}));
  try {
    await stop.flush();
    assert.equal(writes.length, 0);
    const source = browserSession();
    source.settings.sidebarWidth = 290;
    canonical = source;
    for (const listener of listeners) listener("ArdiaHost", "session", { base: {}, session: source });
    await stop.flush();
    assert.equal(writes.length, 0);
    assert.equal(useBrowser.getState().settings.sidebarWidth, 290);
    assert.ok(Array.isArray(useBrowser.getState().settings.customSearchEngines));
    const engine = useBrowser.getState().addCustomEngine({ name: "Utility search", keyword: "utility", url: "https://search.example/?q=%s" });
    assert.equal(typeof engine.id, "string");
    useBrowser.getState().updateSettings({ sidebarWidth: 340 });
    final();
    await new Promise(setImmediate);
    assert.equal(writes.length, 1);
    assert.equal(canonical.settings.sidebarWidth, 340);
    assert.equal(canonical.settings.customSearchEngines.length, 1);
    assert.deepEqual(canonical.windows, source.windows);
    assert.deepEqual(canonical.tabs, source.tabs);
    assert.equal(canonical.focusedWindowId, source.focusedWindowId);
  } finally {
    stop();
    globalThis.window = savedWindow;
    globalThis.document = savedDocument;
  }
});

test("utility close waits for every save acknowledgement and deduplicates repeated close requests", async () => {
  const { createUtilityWindowCloser } = await import("./utilityClose.ts");
  const savedWindow = globalThis.window;
  const savedDocument = globalThis.document;
  globalThis.window = { addEventListener() {}, removeEventListener() {} };
  globalThis.document = { hidden: false, addEventListener() {}, removeEventListener() {} };
  const initial = browserSession();
  let canonical = initial;
  let live = true;
  const pendingWrites = [];
  const closed = [];
  const native = {
    ...host("settings"),
    invoke(method, pair) {
      assert.ok(live);
      assert.equal(method, "session.write");
      return new Promise((resolve) => pendingWrites.push(() => {
        assert.ok(live);
        canonical = mergeSession(pair.base, pair.session, canonical);
        resolve(canonical);
      }));
    },
    onEvent() { return () => {}; },
    onBeforeClose() { return () => {}; },
  };
  useBrowser.getState().hydrate(initial);
  const persistence = startHostPersistence(native, useBrowser, initial, createUtilitySessionSnapshot(useBrowser.getState(), initial));
  const close = createUtilityWindowCloser(persistence.flush, async (id) => { closed.push(id); live = false; });
  try {
    useBrowser.getState().updateSettings({ showFullUrl: true });
    useBrowser.getState().updateProfile("default", { name: "Work" });
    const closing = close("settings");
    assert.equal(close("settings"), closing);
    await new Promise(setImmediate);
    assert.equal(pendingWrites.length, 1);
    assert.deepEqual(closed, []);
    useBrowser.getState().updateSettings({ sidebarWidth: 360 });
    const concurrentSave = persistence.flush();
    pendingWrites.shift()();
    await new Promise(setImmediate);
    assert.equal(pendingWrites.length, 1);
    assert.deepEqual(closed, []);
    pendingWrites.shift()();
    await concurrentSave;
    await closing;
    assert.deepEqual(closed, ["settings"]);
    assert.equal(canonical.settings.showFullUrl, true);
    assert.equal(canonical.settings.sidebarWidth, 360);
    assert.equal(canonical.profiles.default.name, "Work");
    assert.deepEqual(canonical.tabs, initial.tabs);
    assert.equal(canonical.focusedWindowId, initial.focusedWindowId);
  } finally {
    persistence();
    globalThis.window = savedWindow;
    globalThis.document = savedDocument;
  }
});

test("failed utility saves leave the window open and retrying close saves again", async () => {
  const { createUtilityWindowCloser } = await import("./utilityClose.ts");
  const { createSessionWriter } = await import("../../demo/src/hostSession.ts");
  const closed = [];
  let calls = 0;
  const writer = createSessionWriter({
    ...host("settings"),
    async invoke(_method, pair) {
      calls++;
      if (calls === 1) throw new Error("Save rejected");
      return pair.session;
    },
  }, {});
  writer.queue({ settings: { showFullUrl: true } });
  const close = createUtilityWindowCloser(writer.drain, async (id) => { closed.push(id); });
  await assert.rejects(close("settings"), /Save rejected/);
  assert.deepEqual(closed, []);
  await close("settings");
  assert.equal(calls, 2);
  assert.deepEqual(closed, ["settings"]);
  const browserClose = createUtilityWindowCloser(async () => { throw new Error("Unexpected utility save"); }, async (id) => { closed.push(id); });
  await browserClose("main");
  assert.deepEqual(closed, ["settings", "main"]);
});

test("utility close cannot acknowledge native teardown before persistence is ready", async () => {
  const { createUtilityWindowCloser } = await import("./utilityClose.ts");
  const { flushLinuxSession } = await import("./session.ts");
  const closed = [];
  const close = createUtilityWindowCloser(flushLinuxSession, async (id) => { closed.push(id); });
  await assert.rejects(close("settings"), /Session persistence is not ready/);
  assert.deepEqual(closed, []);
});

test("matching canonical state supersedes a settled save failure before retrying utility close", async () => {
  const { createUtilityWindowCloser } = await import("./utilityClose.ts");
  const savedWindow = globalThis.window;
  const savedDocument = globalThis.document;
  globalThis.window = { addEventListener() {}, removeEventListener() {} };
  globalThis.document = { hidden: false, addEventListener() {}, removeEventListener() {} };
  const initial = browserSession();
  const listeners = new Set();
  const closed = [];
  const writes = [];
  const native = {
    ...host("settings"),
    async invoke(method, pair) {
      assert.equal(method, "session.write");
      writes.push(pair);
      throw new Error("Save rejected before canonical event");
    },
    onEvent(listener) { listeners.add(listener); return () => listeners.delete(listener); },
    onBeforeClose() { return () => {}; },
  };
  useBrowser.getState().hydrate(initial);
  const persistence = startHostPersistence(native, useBrowser, initial, createUtilitySessionSnapshot(useBrowser.getState(), initial));
  const close = createUtilityWindowCloser(persistence.flush, async (id) => { closed.push(id); });
  try {
    useBrowser.getState().updateSettings({ showFullUrl: true });
    useBrowser.getState().updateProfile("default", { name: "Work" });
    await assert.rejects(close("settings"), /Save rejected/);
    assert.deepEqual(closed, []);
    const saved = writes[0].session;
    for (const listener of listeners) listener("ArdiaHost", "session", { base: initial, session: saved });
    await close("settings");
    assert.equal(writes.length, 1);
    assert.deepEqual(closed, ["settings"]);
    assert.equal(useBrowser.getState().settings.showFullUrl, true);
    assert.equal(useBrowser.getState().profiles.default.name, "Work");
  } finally {
    persistence();
    globalThis.window = savedWindow;
    globalThis.document = savedDocument;
  }
});

test("canonical satisfaction cannot bypass an in-flight save rejection", async () => {
  const { createUtilityWindowCloser } = await import("./utilityClose.ts");
  const { createSessionWriter } = await import("../../demo/src/hostSession.ts");
  const saved = { settings: { showFullUrl: true } };
  const closed = [];
  let rejectSave;
  let calls = 0;
  const writer = createSessionWriter({
    ...host("settings"),
    invoke() {
      calls++;
      return new Promise((_resolve, reject) => { rejectSave = reject; });
    },
  }, {});
  writer.queue(saved);
  const close = createUtilityWindowCloser(writer.drain, async (id) => { closed.push(id); });
  const closing = close("settings");
  await new Promise(setImmediate);
  writer.acceptCanonical(saved);
  writer.reconcile(saved, false);
  assert.equal(close("settings"), closing);
  assert.deepEqual(closed, []);
  rejectSave(new Error("In-flight save rejected"));
  await assert.rejects(closing, /In-flight save rejected/);
  assert.deepEqual(closed, []);
  await close("settings");
  assert.equal(calls, 1);
  assert.deepEqual(closed, ["settings"]);
});

test("a matching older canonical event cannot discard newer unsaved edits after failure", async () => {
  const { createUtilityWindowCloser } = await import("./utilityClose.ts");
  const { createSessionWriter } = await import("../../demo/src/hostSession.ts");
  const saved = { settings: { showFullUrl: true } };
  const newer = { settings: { showFullUrl: true, sidebarWidth: 360 } };
  const writes = [];
  const closed = [];
  let acknowledge;
  const writer = createSessionWriter({
    ...host("settings"),
    invoke(_method, pair) {
      writes.push(pair);
      if (writes.length === 1) return Promise.reject(new Error("Save rejected"));
      return new Promise((resolve) => { acknowledge = () => resolve(pair.session); });
    },
  }, {});
  writer.queue(saved);
  const close = createUtilityWindowCloser(writer.drain, async (id) => { closed.push(id); });
  await assert.rejects(close("settings"), /Save rejected/);
  writer.acceptCanonical(saved);
  writer.reconcile(newer, true);
  const closing = close("settings");
  await new Promise(setImmediate);
  assert.equal(writes.length, 2);
  assert.deepEqual(writes[1].session, newer);
  assert.deepEqual(closed, []);
  acknowledge();
  await closing;
  assert.deepEqual(closed, ["settings"]);
});
