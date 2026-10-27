import assert from "node:assert/strict";
import test from "node:test";
import { createHostDocuments } from "./hostDocuments.ts";
import { resolveHostModule } from "./hostModules.ts";
import {
  SYNC_CAPABILITIES,
  base32,
  keyringMessage,
  loadSyncDeviceName,
  newSyncDeviceId,
  presentSyncState,
  refreshSyncDocuments,
  syncDeviceName,
  syncModuleMethod,
  withChosenSyncFolder,
} from "./hostSync.ts";
import { NO_AUTHORITY_MESSAGE, REMOTE_METHODS, startSyncAuthority } from "./syncAuthority.ts";

const PHRASE = "legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth title";
const fast = { set: (callback, ms) => setTimeout(callback, Math.min(ms, 15)), clear: (handle) => clearTimeout(handle) };
const tick = (ms = 30) => new Promise((resolve) => setTimeout(resolve, ms));

function fakeHost(reply = () => null) {
  const calls = [];
  return {
    calls,
    bootstrap: { windowId: "w1", profileId: "default", capabilities: [...SYNC_CAPABILITIES] },
    call: async () => "{}",
    invoke: async (method, args) => {
      calls.push({ method, args });
      return reply(method, args);
    },
    onEvent: () => () => {},
  };
}

function store(initial) {
  let state = initial;
  const listeners = new Set();
  return {
    getState: () => state,
    setState(next, replace) {
      state = replace ? next : { ...state, ...next };
      for (const listener of listeners) listener(state);
    },
    subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
  };
}

function fakeLocks() {
  let held = false;
  const queue = [];
  return {
    request(_name, _options, callback) {
      return new Promise((resolve, reject) => {
        const run = async () => {
          held = true;
          try {
            resolve(await callback());
          } catch (error) {
            reject(error);
          } finally {
            held = false;
            queue.shift()?.();
          }
        };
        if (held) queue.push(run);
        else void run();
      });
    },
  };
}

function nativeLease(ttl = 200) {
  let holder = "";
  let expires = 0;
  return {
    claims: [],
    claim(caller) {
      this.claims.push(caller);
      const now = Date.now();
      if (holder && holder !== caller && now < expires) return { granted: false };
      holder = caller;
      expires = now + ttl;
      return { granted: true };
    },
    release(caller) {
      if (holder === caller) holder = "";
    },
    holds: (caller) => holder === caller && Date.now() < expires,
  };
}

function renderer(name, { candidate, locks, lease, engines, channelName }) {
  const mirror = store({ status: "starting", error: null, folder: null });
  const log = [];
  const authority = startSyncAuthority({
    id: name,
    candidate,
    locks,
    channel: new BroadcastChannel(channelName),
    mirror,
    timers: fast,
    report: () => {},
    claim: async () => lease.claim(name),
    release: async () => lease.release(name),
    become: async () => {
      engines.push(name);
      const real = store({ status: "off", error: null, folder: null, owner: name });
      const remote = Object.fromEntries(REMOTE_METHODS.map((method) => [method, async (...args) => {
        log.push([method, args]);
        if (!lease.holds(name)) throw new Error("ERR_SYNC_NOT_AUTHORITY");
        if (method === "turnOnSync") real.setState({ ...real.getState(), status: "starting", folder: args[0] }, true);
        return method === "turnOnSync" ? { ok: true } : method === "enterRecoveryPhrase" ? { ok: true } : null;
      }]));
      return { store: real, remote };
    },
  });
  return { name, authority, mirror, log };
}

test("the sync module adapter keeps device ids local and sends every other call as a typed sync invoke", async () => {
  const host = fakeHost((method) => (method === "ArdiaSync.deviceName" ? "  tux-laptop  " : method === "ArdiaSync.folderInfo" ? { path: "/x" } : true));
  const id = syncModuleMethod(host, "ArdiaSync", "newDeviceId")();
  assert.match(id, /^[a-z2-7]{26}$/);
  assert.notEqual(id, syncModuleMethod(host, "ArdiaSync", "newDeviceId")());
  assert.equal(base32(new Uint8Array([0xff, 0, 0x55])), "74afk");
  assert.equal(newSyncDeviceId((bytes) => bytes.fill(0)), "aaaaaaaaaaaaaaaaaaaaaaaaaa");
  assert.equal(syncDeviceName(), "Linux");
  assert.equal(await loadSyncDeviceName(host), "tux-laptop");
  assert.equal(syncModuleMethod(host, "ArdiaSync", "deviceName")(), "tux-laptop");
  assert.equal(await syncModuleMethod(host, "ArdiaSync", "readPasswords")("1"), null);
  assert.equal(await syncModuleMethod(host, "ArdiaSync", "savePassword")("1", "https://a", "u", "sealed", false), false);
  assert.equal(syncModuleMethod(host, "ArdiaSync", "nope"), undefined);
  assert.equal(syncModuleMethod(host, "ArdiaPasswords", "write"), undefined);
  host.calls.length = 0;
  await resolveHostModule(host, "ArdiaSync", "write")("/sync", "app", "{}");
  await resolveHostModule(host, "ArdiaSync", "folderInfo")(undefined);
  await resolveHostModule(host, "ArdiaSync", "enterPhrase")("aaaaaaaaaaaaaaaaaaaaaaaaaa", PHRASE, "/sync");
  assert.deepEqual(host.calls, [
    { method: "ArdiaSync.write", args: { args: ["/sync", "app", "{}"] } },
    { method: "ArdiaSync.folderInfo", args: { args: [null] } },
    { method: "ArdiaSync.enterPhrase", args: { args: ["aaaaaaaaaaaaaaaaaaaaaaaaaa", PHRASE, "/sync"] } },
  ]);
  assert.equal(host.calls.some((call) => call.method.startsWith("engine.")), false);
});

test("a folder chosen in another window is handed to the authority's picker exactly once", async () => {
  const host = fakeHost(() => "/from-native-picker");
  const choose = syncModuleMethod(host, "ArdiaSync", "chooseFolder");
  const picked = await withChosenSyncFolder("/home/tux/Sync", async () => [await choose("/old"), await choose("/old")]);
  assert.deepEqual(picked, ["/home/tux/Sync", "/from-native-picker"]);
  assert.equal(await choose(null), "/from-native-picker");
  assert.deepEqual(host.calls.map((call) => call.args), [{ args: ["/old"] }, { args: [null] }]);
});

test("keyring problems read as keyring problems, not as a Keychain on a Mac", () => {
  const secret = (keyStoreStatus, lastKeyStatus = keyStoreStatus) => ({ keyStore: "secretService", keyStoreStatus, lastKeyStatus, unlocked: false, unreadable: false, passwords: false });
  assert.match(keyringMessage(secret("unavailable"), true), /no keyring is running/);
  assert.match(keyringMessage(secret("ok", "locked"), true), /keyring is locked/);
  assert.equal(keyringMessage(secret("ok"), true), null);
  assert.equal(keyringMessage({ ...secret("unavailable"), keyStore: "file" }, false), null);
  const locked = { status: "locked", error: "Enter your recovery phrase to keep syncing on this Mac." };
  assert.match(presentSyncState(locked, secret("unavailable")).error, /Start a Secret Service keyring/);
  assert.equal(presentSyncState(locked, secret("ok")), locked);
  assert.equal(presentSyncState({ status: "updated", error: null }, secret("unavailable")).error, null);
});

test("one renderer runs sync, the others mirror it and forward their actions, and it fails over when that window closes", async () => {
  const channelName = `sync-${Math.random()}`;
  const locks = fakeLocks();
  const lease = nativeLease();
  const engines = [];
  const a = renderer("a", { candidate: true, locks, lease, engines, channelName });
  const b = renderer("b", { candidate: true, locks, lease, engines, channelName });
  const settings = renderer("settings", { candidate: false, locks, lease, engines, channelName });
  const privateWindow = renderer("private", { candidate: false, locks, lease, engines, channelName });
  await tick(80);
  assert.deepEqual(engines, ["a"]);
  assert.equal(a.authority.role(), "authority");
  assert.equal(b.authority.role(), "claiming");
  assert.equal(settings.authority.role(), "client");
  assert.equal(settings.mirror.getState().owner, "a");
  assert.equal(lease.claims.includes("settings") || lease.claims.includes("private"), false);

  assert.deepEqual(await settings.authority.call("turnOnSync", ["/home/tux/Sync"]), { ok: true });
  assert.deepEqual(await b.authority.call("enterRecoveryPhrase", ["/home/tux/Sync", PHRASE]), { ok: true });
  assert.deepEqual(a.log.map(([method]) => method), ["turnOnSync", "enterRecoveryPhrase"]);
  assert.equal(settings.log.length + b.log.length + privateWindow.log.length, 0);
  await tick();
  assert.equal(settings.mirror.getState().folder, "/home/tux/Sync");
  assert.equal(privateWindow.mirror.getState().folder, "/home/tux/Sync");
  await assert.rejects(settings.authority.call("evaluate", []), new RegExp(NO_AUTHORITY_MESSAGE.slice(0, 20)));

  a.authority.stop();
  await tick(120);
  assert.deepEqual(engines, ["a", "b"]);
  assert.equal(b.authority.role(), "authority");
  assert.equal(settings.mirror.getState().owner, "b");
  assert.equal(await settings.authority.call("syncNow", []), null);
  assert.deepEqual(b.log.map(([method]) => method), ["syncNow"]);

  b.authority.stop();
  await tick(60);
  await assert.rejects(settings.authority.call("syncNow", []), { message: NO_AUTHORITY_MESSAGE });
  settings.authority.stop();
  privateWindow.authority.stop();
});

test("a candidate waits for a stale native lease to expire before starting sync", async () => {
  const channelName = `sync-${Math.random()}`;
  const lease = nativeLease(150);
  lease.claim("reloaded-window#1");
  const engines = [];
  const fresh = renderer("reloaded-window#2", { candidate: true, locks: fakeLocks(), lease, engines, channelName });
  await tick(60);
  assert.deepEqual(engines, []);
  assert.equal(fresh.authority.role(), "claiming");
  await tick(250);
  assert.deepEqual(engines, ["reloaded-window#2"]);
  fresh.authority.stop();
});

test("without Web Locks the native lease alone keeps a single authority", async () => {
  const channelName = `sync-${Math.random()}`;
  const lease = nativeLease(5000);
  const engines = [];
  const first = renderer("first", { candidate: true, lease, engines, channelName });
  const second = renderer("second", { candidate: true, lease, engines, channelName });
  await tick(120);
  assert.deepEqual(engines, ["first"]);
  assert.equal(second.authority.role(), "claiming");
  first.authority.stop();
  second.authority.stop();
});

test("a private window is refused natively and never becomes the authority", async () => {
  const mirror = store({ status: "starting", error: null });
  const roles = [];
  const authority = startSyncAuthority({
    id: "private",
    candidate: true,
    channel: null,
    mirror,
    timers: fast,
    report: () => {},
    claim: async () => {
      throw Object.assign(new Error("Sync isn't available in private windows"), { code: "ERR_PRIVATE_SERVICE" });
    },
    release: async () => {},
    become: async () => assert.fail("a private window started sync"),
    onRole: (role) => roles.push(role),
  });
  await tick();
  assert.deepEqual(roles, ["unavailable"]);
  await assert.rejects(authority.call("syncNow", []), { message: NO_AUTHORITY_MESSAGE });
  authority.stop();
});

test("refreshing a stored document waits for queued writes and mirrors deletions", async () => {
  const disk = new Map([["sync.json", "{\"v\":1,\"enabled\":true}"]]);
  const order = [];
  const host = {
    bootstrap: { windowId: "w", profileId: "default", documents: { "sync.json": "{\"v\":1,\"enabled\":false}", "sync-journal-1.nns": "stale" } },
    invoke: async (method, { args }) => {
      order.push(`${method}:${args[0]}`);
      if (method === "ArdiaShell.writeDocument") {
        await tick(10);
        disk.set(args[0], args[1]);
        return null;
      }
      if (method === "ArdiaShell.readDocument") return disk.get(args[0]) ?? null;
      if (method === "ArdiaShell.removeDocument") return null;
      return 42;
    },
  };
  const documents = createHostDocuments(host);
  assert.equal(documents.read("sync.json"), "{\"v\":1,\"enabled\":false}");
  void documents.write("sync-state.nns", "sealed");
  assert.equal(await documents.refresh("sync-state.nns"), true);
  assert.deepEqual(order.slice(0, 2), ["ArdiaShell.writeDocument:sync-state.nns", "ArdiaShell.readDocument:sync-state.nns"]);
  assert.equal(await documents.refresh("sync.json"), true);
  assert.equal(documents.read("sync.json"), "{\"v\":1,\"enabled\":true}");
  assert.equal(await documents.refresh("sync-journal-1.nns"), false);
  assert.equal(documents.read("sync-journal-1.nns"), null);
  assert.equal(await documents.refresh("live.json"), false);
  host.invoke = async () => 42;
  await assert.rejects(documents.refresh("sync.json"), { code: "ERR_BAD_REPLY" });
});

test("a candidate that can't start sync keeps the lock and retries with backoff", async () => {
  const channelName = `sync-${Math.random()}`;
  const lease = nativeLease(5000);
  const locks = fakeLocks();
  let attempts = 0;
  const releases = [];
  const mirror = store({ status: "starting", error: null });
  const authority = startSyncAuthority({
    id: "flaky",
    candidate: true,
    locks,
    channel: new BroadcastChannel(channelName),
    mirror,
    timers: fast,
    report: () => {},
    claim: async () => lease.claim("flaky"),
    release: async () => {
      releases.push(attempts);
      lease.release("flaky");
    },
    become: async () => {
      attempts += 1;
      if (attempts < 3) throw new Error("ERR_DOCUMENT_READ");
      return { store: store({ status: "off", error: null, owner: "flaky" }), remote: Object.fromEntries(REMOTE_METHODS.map((m) => [m, async () => null])) };
    },
  });
  const engines = [];
  const other = renderer("other", { candidate: true, locks, lease, engines, channelName });
  await tick(250);
  assert.equal(attempts, 3);
  assert.deepEqual(releases, [1, 2]);
  assert.equal(authority.role(), "authority");
  assert.deepEqual(engines, []);
  authority.stop();
  other.authority.stop();
});

test("refreshing sync documents drops journal parts stranded after a gap", async () => {
  const disk = new Set(["sync.json", "sync-state.nns", "sync-journal-1.nns", "sync-journal-2.nns", "sync-journal-4.nns", "sync-journal-7.nns"]);
  const refreshed = [];
  const removed = [];
  const result = await refreshSyncDocuments({
    refresh: async (name) => {
      refreshed.push(name);
      return disk.has(name);
    },
    remove: async (name) => {
      removed.push(name);
      disk.delete(name);
    },
    maxParts: 12,
    batch: 5,
  });
  assert.deepEqual(result, { parts: 2, removed: ["sync-journal-4.nns", "sync-journal-7.nns"] });
  assert.deepEqual(removed, ["sync-journal-4.nns", "sync-journal-7.nns"]);
  assert.ok(refreshed.includes("sync.json") && refreshed.includes("sync-state.nns") && refreshed.includes("sync-journal.nns"));
  assert.equal(refreshed.filter((name) => /^sync-journal-\d+\.nns$/.test(name)).length, 12);
  await assert.rejects(refreshSyncDocuments({ refresh: async () => { throw new Error("read failed"); }, remove: async () => {}, maxParts: 3 }), /read failed/);
});
