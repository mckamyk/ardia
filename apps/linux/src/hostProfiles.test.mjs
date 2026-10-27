import assert from "node:assert/strict";
import test from "node:test";
import { useBrowser } from "../../browser/src/state/browser/store.ts";
import { engineProfile, setSharedDataIds } from "../../browser/src/state/browser/model.ts";
import { initialHostSession, snapshotHostSession, startHostPersistence } from "./hostSession.ts";
import { createHostWindowOpener, installProfileWindows, privateWindowSeed, profileWindowFor, withPrivateWindow } from "./hostWindowProfiles.ts";

const S = () => useBrowser.getState();
const tick = () => new Promise(setImmediate);
const quiet = (run) => async (...args) => {
  const error = console.error;
  console.error = () => {};
  try { return await run(...args); } finally { console.error = error; }
};

function regularSession() {
  const initial = initialHostSession({ bootstrap: { windowId: "main", profileId: "default", startup: false } }, null);
  S().hydrate(initial);
  const work = S().createProfile({ name: "Work" });
  const shared = S().createProfile({ name: "Side", shareWith: "default" });
  S().setFocusedWindow("main");
  return { work, shared };
}

function fakeHost({ reject, bootstrap = { windowId: "main", profileId: "default", startup: false } } = {}) {
  const calls = [];
  return {
    calls,
    bootstrap,
    invoke(method, args) {
      calls.push([method, structuredClone(args)]);
      if (method === "ArdiaShell.openWindow" && reject) return Promise.reject(Object.assign(new Error(reject), { code: "ERR_PROFILE_UNAVAILABLE" }));
      return Promise.resolve(null);
    },
    call: async () => "null",
    onEvent: () => () => {},
  };
}

test("a window whose native creation fails is closed in shell state so no phantom window remains", quiet(async () => {
  const { work } = regularSession();
  const id = S().createWindow({ profileId: work, background: true });
  const host = fakeHost({ reject: "Chrome could not open this profile" });
  const events = [];
  const open = createHostWindowOpener(host, () => S(), (event) => events.push(event));
  await open(id, { profile: engineProfile(S().windows[id].profileId) });
  assert.deepEqual(host.calls, [["ArdiaShell.openWindow", { args: [id, { profile: work }] }]]);
  assert.deepEqual(events, [{ type: "close", id }]);

  const opened = fakeHost();
  const none = [];
  await createHostWindowOpener(opened, () => S(), (event) => none.push(event))(id, { profile: work });
  assert.deepEqual(none, [], "a created window is left alone");

  const gone = [];
  await createHostWindowOpener(fakeHost({ reject: "no" }), () => S(), (event) => gone.push(event))("w-missing", { profile: "" });
  assert.deepEqual(gone, [], "nothing to reconcile for a window the shell no longer has");
}));

test("private windows open with their own seed and never fall back to a regular window", quiet(async () => {
  const { work } = regularSession();
  S().setFocusedWindow("main");
  const id = S().createWindow({ incognito: true, profileId: work, url: "https://private.test/" });
  const w = S().windows[id];
  assert.equal(engineProfile(w.profileId), `incognito:${id}@${work}`);
  const host = fakeHost();
  await createHostWindowOpener(host, () => S(), () => {})(id, { incognito: true, profile: engineProfile(w.profileId) });
  const [, { args: [sentId, options] }] = host.calls[0];
  assert.equal(sentId, id);
  assert.equal(options.privateWindow.window.id, id);
  assert.equal(options.privateWindow.window.incognito, true);
  assert.deepEqual(Object.keys(options.privateWindow.tabs), w.tabIds);
  assert.equal(Object.values(options.privateWindow.tabs)[0].url, "https://private.test/");
  assert.equal(options.privateWindow.window.chromeWindow, undefined);

  const events = [];
  const noSeed = fakeHost();
  await createHostWindowOpener(noSeed, () => ({ windows: { [id]: { ...w, incognito: false } }, tabs: S().tabs }), (event) => events.push(event))(id, { incognito: true, profile: engineProfile(w.profileId) });
  assert.deepEqual(noSeed.calls, [], "no regular-window state is sent as a private window");
  assert.deepEqual(events, [{ type: "close", id }]);

  assert.equal(privateWindowSeed(S(), "main"), undefined);
}));

test("a private window renderer starts from its seed, and a missing seed fails instead of opening a regular window", () => {
  const { work } = regularSession();
  const saved = snapshotHostSession(S());
  const id = S().createWindow({ incognito: true, profileId: work, url: "https://private.test/" });
  const seed = privateWindowSeed(S(), id);
  const pageProfile = engineProfile(S().windows[id].profileId);
  const bootstrap = { windowId: id, profileId: "default", startup: false, pageProfile, privateWindow: seed };
  const session = initialHostSession({ bootstrap }, saved);
  assert.equal(session.windows[id].incognito, true);
  assert.equal(session.windows[id].profileId, S().windows[id].profileId);
  assert.equal(session.focusedWindowId, id);
  assert.deepEqual(session.windows[id].tabIds, seed.window.tabIds);
  assert.equal(session.windows.main.incognito, false, "public windows stay as they were");
  assert.ok(!Object.values(session.windows).some((x) => x.id !== id && x.incognito));

  assert.throws(() => initialHostSession({ bootstrap: { ...bootstrap, privateWindow: undefined } }, saved), /private window state/);
  assert.throws(() => initialHostSession({ bootstrap: { ...bootstrap, pageProfile: `incognito:${id}@` } }, saved), /private window state/);
  const foreign = { ...seed, tabs: { ...seed.tabs, intruder: { ...Object.values(seed.tabs)[0], id: "intruder", profileId: work } } };
  const filtered = withPrivateWindow({ bootstrap: { ...bootstrap, privateWindow: foreign } }, saved);
  assert.equal(filtered.tabs.intruder, undefined, "only the private window's own tabs are adopted");
  assert.equal(withPrivateWindow({ bootstrap: { windowId: "main", profileId: "default", pageProfile: "" } }, saved), undefined);
});

test("private windows, their tabs and their closed entries never reach session persistence", async () => {
  const { work } = regularSession();
  const id = S().createWindow({ incognito: true, profileId: work, url: "https://private.test/" });
  const seed = privateWindowSeed(S(), id);
  const saved = snapshotHostSession(S());
  const bootstrap = { windowId: id, profileId: "default", startup: false, pageProfile: engineProfile(S().windows[id].profileId), privateWindow: seed };
  S().hydrate(initialHostSession({ bootstrap }, saved));
  const privateTab = S().windows[id].tabIds[0];
  S().closeTabs([privateTab]);
  S().createWindow({ incognito: true, profileId: "default", url: "https://second-private.test/" });
  const writes = [];
  const host = {
    bootstrap,
    invoke: async (method, pair) => { writes.push([method, structuredClone(pair)]); return pair.session; },
    onEvent: () => () => {},
  };
  const savedWindow = globalThis.window;
  const savedDocument = globalThis.document;
  globalThis.window = { addEventListener() {}, removeEventListener() {} };
  globalThis.document = { hidden: false, addEventListener() {}, removeEventListener() {} };
  const persistence = startHostPersistence(host, useBrowser, saved);
  try {
    S().updateSettings({ sidebarWidth: 301 });
    await persistence.flush();
    await tick();
  } finally {
    persistence();
    globalThis.window = savedWindow;
    globalThis.document = savedDocument;
  }
  assert.ok(writes.length > 0);
  for (const [method, { session }] of writes) {
    assert.equal(method, "session.write");
    const json = JSON.stringify(session);
    assert.ok(!json.includes("incognito:"), "no private profile identity is written");
    assert.ok(!json.includes("private.test"), "no private page is written");
    assert.ok(!Object.values(session.windows).some((w) => w.incognito || w.originalProfileId));
  }
  assert.ok(S().windows[id], "the private window stays in this renderer's memory");
});

test("an isolated profile opens in its own native window while shared-data profiles switch in place", () => {
  const { work, shared } = regularSession();
  setSharedDataIds({ [shared]: "default" });
  try {
    const focused = [];
    installProfileWindows(useBrowser, (id) => focused.push(id));
    const before = S().windowOrder.length;
    S().switchProfile("main", shared);
    assert.equal(S().windows.main.profileId, shared, "a profile sharing the window's data switches in place");
    assert.equal(S().windowOrder.length, before);
    S().switchProfile("main", "default");

    S().switchProfile("main", work);
    assert.equal(S().windows.main.profileId, "default", "the default-profile window never hosts the isolated profile");
    assert.equal(S().windowOrder.length, before + 1);
    const workWindow = S().windowOrder.find((id) => S().windows[id].profileId === work);
    assert.ok(workWindow);
    assert.ok(S().windows[workWindow].tabIds.every((t) => S().tabs[t].profileId === work));
    assert.equal(profileWindowFor(S(), work), workWindow);

    S().switchProfile("main", work);
    assert.equal(S().windowOrder.length, before + 1, "an existing window for that profile is reused");
    assert.deepEqual(focused, [workWindow]);

    const tab = S().windows.main.tabIds[0];
    useBrowser.setState((s) => ({ tabs: { ...s.tabs, [tab]: { ...s.tabs[tab], url: "https://move.test/" } } }));
    S().moveTabToProfile(tab, work);
    assert.equal(S().tabs[tab], undefined, "the source tab leaves the default-profile window");
    const moved = Object.values(S().tabs).find((t) => t.url === "https://move.test/");
    assert.ok(moved && moved.profileId === work && moved.windowId !== "main");
    assert.ok(Object.values(S().windows).every((w) => w.incognito || w.tabIds.every((t) => engineProfile(S().tabs[t].profileId) === engineProfile(w.profileId))),
      "every regular window keeps one engine profile");
  } finally {
    setSharedDataIds({});
  }
});
