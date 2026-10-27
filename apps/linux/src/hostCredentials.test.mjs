import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { readFileSync } from "node:fs";
import test from "node:test";
import { runInNewContext } from "node:vm";
import { resolveHostModule } from "./hostModules.ts";
import { CREDENTIAL_CAPABILITIES, autofillStatus, credentialNotice, passwordStatus } from "./hostCredentials.ts";

const bootstrap = readFileSync(new URL("../../../engine/ardiacore-linux/src/ardia/linux/bootstrap.js", import.meta.url), "utf8");
const PASSWORD = "correct horse battery staple";
const NEW_PASSWORD = "tr0ub4dor&3";
const CARD = "4111111111111111";

const native = {
  mode: "ready",
  auth: "granted",
  passwords: [{ origin: "https://example.test", username: "me", password: PASSWORD }],
  addresses: [{ id: "addr-1", name: "Ada" }],
  cards: [{ id: "card-1", network: "visa", last4: "1111", number: CARD }],
  wire: [],
};

const unavailable = { code: "ERR_PASSWORDS_UNAVAILABLE", message: "The system keyring is locked or unavailable, so saved passwords can't be read" };
const autofillUnavailable = { code: "ERR_AUTOFILL_UNAVAILABLE", message: "No encryption key is available to read or protect saved secrets" };

function engineCall(name, args) {
  const locked = native.mode === "locked";
  if (name.startsWith("passwords.") && locked && name !== "passwords.autofillEnabled") return [false, unavailable];
  if (name === "autofill.cards" && locked) return [false, autofillUnavailable];
  switch (name) {
    case "passwords.list": return [true, JSON.stringify({ passwords: native.passwords.map(({ origin, username }) => ({ origin, username })) })];
    case "passwords.remove": native.passwords = native.passwords.filter((p) => !(p.origin === args.origin && p.username === args.username)); return [true, JSON.stringify({ ok: true })];
    case "passwords.neverSaveOrigins": return [true, JSON.stringify({ origins: ["https://never.test"] })];
    case "passwords.autofillEnabled": return [true, "true"];
    case "autofill.addresses": return [true, JSON.stringify({ addresses: native.addresses })];
    case "autofill.cards": return [true, JSON.stringify({ cards: native.cards.map(({ number, ...card }) => card) })];
    case "autofill.settings": return [true, JSON.stringify({ addresses: true, cards: false })];
    default: return [false, { code: "ERR_UNKNOWN_CALL", message: "This call is unavailable on Linux" }];
  }
}

function invoke(method, args) {
  const granted = native.auth === "granted";
  if (native.mode === "locked" && method.startsWith("ArdiaPasswords.") && method !== "ArdiaPasswords.status") return [false, unavailable];
  const find = () => native.passwords.find((p) => p.origin === args[1] && p.username === args[2]);
  switch (method) {
    case "ArdiaPasswords.status": return [true, { passwords: native.mode === "locked" ? { available: false, reason: "keyringLocked", ...unavailable } : { available: true }, reauth: native.auth !== "unavailable", backend: "basic" }];
    case "ArdiaAutofill.status": return [true, { addresses: { available: true }, cards: native.mode === "locked" ? { available: false, reason: "encryptionUnavailable", ...autofillUnavailable } : { available: true }, reauth: true, backend: "gnome-libsecret" }];
    case "ArdiaPasswords.unlock": return native.auth === "unavailable" ? [false, { code: "ERR_REAUTH_UNAVAILABLE", message: "OS authentication is unavailable on this platform" }] : [true, { unlocked: granted }];
    case "ArdiaPasswords.reveal": return [true, { password: granted ? find()?.password ?? null : null }];
    case "ArdiaPasswords.save": native.passwords.push({ origin: args[1], username: args[2], password: args[3] }); return [true, { ok: true }];
    case "ArdiaPasswords.update": {
      if (!granted) return [false, { code: "ERR_REAUTH_DENIED", message: "Not authenticated" }];
      const entry = find();
      if (args[4] !== null) entry.password = args[4];
      if (args[3] !== null) entry.username = args[3];
      return [true, { ok: true }];
    }
    case "ArdiaPasswords.export": return [true, granted ? { status: "succeeded", path: "/tmp/scratch/Ardia Passwords.csv" } : { status: "reauthFailed" }];
    case "ArdiaAutofill.saveCard": native.cards.push({ id: "card-2", network: "visa", last4: args[2].slice(-4), number: args[2] }); return [true, { id: "card-2" }];
    case "ArdiaAutofill.cardNumber": return [true, { number: granted ? native.cards.find((c) => c.id === args[1])?.number ?? null : null, authenticated: granted }];
    default: return [false, { code: "ERR_UNKNOWN_CALL", message: "unknown" }];
  }
}

const logs = [];
const capture = (...parts) => logs.push(parts.map((part) => (typeof part === "string" ? part : JSON.stringify(part) ?? String(part))).join(" "));

const context = {
  location: { protocol: "chrome:", hostname: "ardia" },
  crypto: { randomUUID },
  __ardiaBootstrap: { windowId: "settings", profileId: "default", capabilities: [...CREDENTIAL_CAPABILITIES] },
  setTimeout, clearTimeout, addEventListener() {},
  console: { log: capture, warn: capture, error: capture, info: capture, debug: capture },
  chrome: {
    send(name, [id, operation, payload]) {
      assert.equal(name, "ardiaRequest");
      native.wire.push(JSON.stringify([operation, payload]));
      const [ok, result] = operation === "engine.call"
        ? engineCall(payload.name, payload.args ? JSON.parse(payload.args) : {})
        : invoke(payload.method, payload.args.args);
      queueMicrotask(() => context.__ardiaReply(id, ok, result));
    },
  },
};
context.window = context;
context.top = context;
runInNewContext(bootstrap, context);
const host = context.__ardiaHost;

const moduleFor = (name) => new Proxy({}, { get: (_target, key) => (typeof key === "string" ? resolveHostModule(host, name, key) : undefined) });
globalThis.acTestNativeModules = { ArdiaPasswords: moduleFor("ArdiaPasswords"), ArdiaAutofill: moduleFor("ArdiaAutofill") };
const originalConsole = { log: console.log, warn: console.warn, error: console.error };
Object.assign(console, { log: capture, warn: capture, error: capture });
const passwords = await import("../../../packages/ardiacore/src/passwords.ts");
const autofill = await import("../../../packages/ardiacore/src/autofill.ts");

const reset = () => {
  native.mode = "ready";
  native.auth = "granted";
  native.wire.length = 0;
};

function genericWire() {
  return native.wire.filter((entry) => entry.startsWith('["engine.call"'));
}

test("listing, removing and settings use the generic domain route with macOS-shaped replies", async () => {
  reset();
  assert.deepEqual(await passwords.listPasswords(""), [{ origin: "https://example.test", username: "me", created: 0, modified: 0 }]);
  assert.deepEqual(await passwords.getNeverSavePasswordOrigins(""), ["https://never.test"]);
  assert.equal(await passwords.getPasswordAutofill(""), true);
  assert.deepEqual(await autofill.listAddresses(""), [{ id: "addr-1", name: "Ada", created: 0, modified: 0 }]);
  assert.deepEqual(await autofill.listCards(""), [{ id: "card-1", network: "visa", last4: "1111", created: 0, modified: 0 }]);
  assert.deepEqual(await autofill.getAutofillSettings(""), { addresses: true, cards: false });
  assert.deepEqual(genericWire().map((entry) => JSON.parse(entry)[1].name), [
    "passwords.list", "passwords.neverSaveOrigins", "passwords.autofillEnabled", "autofill.addresses", "autofill.cards", "autofill.settings",
  ]);
});

test("secret operations travel only through typed native methods and never the generic route", async () => {
  reset();
  assert.equal(await passwords.unlockPasswords(""), true);
  assert.equal(await passwords.getPassword("", "https://example.test", "me"), PASSWORD);
  assert.equal(await passwords.savePassword("", "https://new.test", "you", NEW_PASSWORD), true);
  assert.equal(await passwords.updatePassword("", "https://new.test", "you", { password: `${NEW_PASSWORD}!` }), true);
  assert.deepEqual(await passwords.exportPasswords(""), { status: "succeeded", path: "/tmp/scratch/Ardia Passwords.csv" });
  assert.equal(await autofill.saveCard("", { name: "Ada", expMonth: 4, expYear: 2031 }, "4000000000000002"), "card-2");
  assert.equal(await autofill.revealCardNumber("", "card-1"), CARD);
  for (const entry of genericWire()) {
    for (const secret of [PASSWORD, NEW_PASSWORD, CARD, "4000000000000002"]) assert.equal(entry.includes(secret), false, entry);
  }
  const typed = native.wire.filter((entry) => entry.startsWith('["host.invoke"')).map((entry) => JSON.parse(entry)[1]);
  assert.deepEqual(typed.map((entry) => entry.method), [
    "ArdiaPasswords.unlock", "ArdiaPasswords.reveal", "ArdiaPasswords.save", "ArdiaPasswords.update", "ArdiaPasswords.export",
    "ArdiaAutofill.saveCard", "ArdiaAutofill.cardNumber",
  ]);
  assert.deepEqual(typed[1].args.args, ["default", "https://example.test", "me"]);
  assert.deepEqual(typed[3].args.args, ["default", "https://new.test", "you", null, `${NEW_PASSWORD}!`]);
  assert.deepEqual(typed[5].args.args, ["default", { name: "Ada", expMonth: 4, expYear: 2031 }, "4000000000000002"]);
  assert.equal(genericWire().length, 0);
});

test("generic secret verbs are refused before anything reaches the host", async () => {
  reset();
  const module = moduleFor("ArdiaPasswords");
  for (const verb of ["save", "update", "add"]) {
    await assert.rejects(module.call(verb, "", JSON.stringify({ origin: "https://a.test", password: PASSWORD })), { code: "ERR_SECRET_ROUTE" });
  }
  await assert.rejects(moduleFor("ArdiaAutofill").call("saveCard", "", JSON.stringify({ number: CARD })), { code: "ERR_SECRET_ROUTE" });
  await assert.rejects(module.call("reveal", "", "{not json"), { code: "ERR_CREDENTIAL_REQUEST" });
  assert.equal(native.wire.length, 0);
});

for (const outcome of ["denied", "cancelled", "expired"]) {
  test(`authentication ${outcome} never reveals, changes or exports`, async () => {
    reset();
    native.auth = outcome;
    assert.equal(await passwords.unlockPasswords(""), false);
    assert.equal(await passwords.getPassword("", "https://example.test", "me"), null);
    assert.equal(await autofill.revealCardNumber("", "card-1"), null);
    assert.deepEqual(await passwords.exportPasswords(""), { status: "reauthFailed" });
    await assert.rejects(passwords.updatePassword("", "https://example.test", "me", { password: NEW_PASSWORD }), { code: "ERR_REAUTH_DENIED" });
    assert.equal(native.passwords.find((p) => p.username === "me").password, PASSWORD);
  });
}

test("unavailable stores reject distinctly from an empty vault", async () => {
  reset();
  const saved = native.passwords;
  native.passwords = [];
  assert.deepEqual(await passwords.listPasswords(""), []);
  native.passwords = saved;
  native.mode = "locked";
  await assert.rejects(passwords.listPasswords(""), { code: "ERR_PASSWORDS_UNAVAILABLE" });
  await assert.rejects(passwords.unlockPasswords(""), { code: "ERR_PASSWORDS_UNAVAILABLE" });
  await assert.rejects(passwords.getPassword("", "https://example.test", "me"), { code: "ERR_PASSWORDS_UNAVAILABLE" });
  await assert.rejects(autofill.listCards(""), { code: "ERR_AUTOFILL_UNAVAILABLE" });
  assert.deepEqual(await autofill.listAddresses(""), [{ id: "addr-1", name: "Ada", created: 0, modified: 0 }]);
  native.auth = "unavailable";
  native.mode = "ready";
  await assert.rejects(passwords.unlockPasswords(""), { code: "ERR_REAUTH_UNAVAILABLE" });
});

test("status drives an honest notice", async () => {
  reset();
  native.mode = "locked";
  const status = await passwordStatus(host, "");
  assert.equal(status.passwords.reason, "keyringLocked");
  assert.match(credentialNotice(status, "passwords"), /keyring is locked/);
  assert.match(credentialNotice(status, "passwords"), /basic password store/);
  const cards = await autofillStatus(host, "");
  assert.match(credentialNotice(cards, "autofill"), /No encryption key/);
  native.mode = "ready";
  assert.equal(credentialNotice(await autofillStatus(host, ""), "autofill"), null);
  assert.equal(credentialNotice({ passwords: { available: true }, reauth: false, backend: "auto" }, "passwords"),
    "System authentication is unavailable, so saved passwords can't be shown, changed or exported.");
  assert.equal(credentialNotice(null, "passwords"), null);
});

test("nothing logged during credential traffic contains a secret", () => {
  Object.assign(console, originalConsole);
  for (const line of logs) {
    for (const secret of [PASSWORD, NEW_PASSWORD, CARD, "4000000000000002"]) assert.equal(line.includes(secret), false, line);
  }
});
