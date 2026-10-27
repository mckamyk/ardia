import { readdir, readFile } from "node:fs/promises";
import { join } from "node:path";
import { restartBrowser, startBrowser } from "../lib/flows.mjs";
import { nonce, until } from "../lib/util.mjs";

const ORIGIN = "https://login.ardia.test";

async function expectOk(promise, label) {
  const reply = await promise;
  if (!reply.ok) throw new Error(`${label} failed: ${reply.code} ${reply.message}`);
  return reply.value;
}

async function expectCode(promise, code, label) {
  const reply = await promise;
  if (reply.ok || reply.code !== code) throw new Error(`${label}: expected ${code}, got ${JSON.stringify(reply)}`);
  return { code: reply.code };
}

async function storeFiles(scratch) {
  const profile = join(scratch, "Default");
  const names = (await readdir(profile).catch(() => [])).filter((name) => /^(Login Data|Web Data)/.test(name));
  return Promise.all(names.map(async (name) => ({ name, bytes: await readFile(join(profile, name)) })));
}

export default {
  id: "F07",
  issue: 17,
  title: "Passwords and autofill management, secret gating and storage at rest",
  deadline: 300000,
  manual: [
    { name: "System authentication gates reveal, edit, export and card numbers", steps: ["Install share/polkit-1/actions/dev.ardia.linux.policy into /usr/share/polkit-1/actions and run a polkit agent (hyprpolkitagent, polkit-gnome or the desktop's own).", "Settings › Passwords › Unlock: the agent asks for your password; entering it lists saved passwords.", "Open a saved password within five minutes: it shows without a second prompt; after five minutes it prompts again.", "Export CSV…: the save dialog appears, then the agent always prompts; the CSV is written with mode 0600.", "Settings › Autofill › a card › Show number: the agent always prompts."], reason: "needs a desktop session, the installed polkit policy and a person at the agent prompt" },
    { name: "Denied, dismissed and timed-out prompts never reveal", steps: ["Deny the agent prompt (wrong password three times), dismiss it, and leave it open for two minutes.", "Each time the password stays hidden, Edit does not save, Export reports nothing written and no file appears, and the card number stays masked."], reason: "needs a person at the agent prompt" },
    { name: "A locked keyring is reported as unavailable", steps: ["Start Ardia with --password-store=gnome-libsecret and a locked or absent Secret Service.", "Settings › Passwords shows 'Passwords are unavailable' with the keyring reason instead of an empty vault, and Unlock does not show 'No saved passwords'."], reason: "needs a real Secret Service session" },
    { name: "Private windows refuse credential management", steps: ["Once private windows land (#62), open Settings from a private window: Passwords and Autofill report ERR_PROFILE_PRIVATE instead of showing the regular profile's data."], reason: "private windows are not merged yet" },
  ],
  async run(t) {
    const run = nonce("f07");
    const password = `pw-${run}-Secret!`;
    const changed = `pw2-${run}-Secret!`;
    const card = "4111111111111111";
    const username = `user-${run}`;
    let state = await startBrowser(t, { name: "credentials" });
    const { shell } = state;

    await t.check("status reports the store available, its backend and that authentication is unavailable in a background instance", async () => {
      const passwords = await expectOk(shell.invoke("ArdiaPasswords.status", { args: [""] }), "ArdiaPasswords.status");
      const autofill = await expectOk(shell.invoke("ArdiaAutofill.status", { args: [""] }), "ArdiaAutofill.status");
      if (!passwords.passwords?.available || passwords.backend !== "basic" || passwords.reauth !== false || passwords.reauthReason !== "background") throw new Error(JSON.stringify(passwords));
      if (!autofill.addresses?.available || !autofill.cards?.available) throw new Error(JSON.stringify(autofill));
      return { passwords, autofill };
    }, { critical: true });

    await t.check("an empty vault lists nothing instead of failing", async () => {
      const value = await expectOk(shell.call("passwords.list"), "passwords.list");
      if (!Array.isArray(value.passwords) || value.passwords.length) throw new Error(JSON.stringify(value));
      return value;
    });

    await t.check("a credential saves through the typed method and lists without its password", async () => {
      await expectOk(shell.invoke("ArdiaPasswords.save", { args: ["", ORIGIN, username, password] }), "ArdiaPasswords.save");
      const value = await until(async () => {
        const reply = await shell.call("passwords.list");
        return reply.ok && reply.value.passwords.some((p) => p.username === username) ? reply.value : false;
      }, { timeout: 10000, label: "saved credential listed" });
      if (JSON.stringify(value).includes(password)) throw new Error("the list carried the password");
      return value;
    }, { critical: true });

    await t.check("a credential is removed and never-save sites are listed", async () => {
      await expectOk(shell.invoke("ArdiaPasswords.save", { args: ["", ORIGIN, `${username}-old`, password] }), "save second credential");
      await expectOk(shell.call("passwords.remove", { origin: ORIGIN, username: `${username}-old` }), "passwords.remove");
      const value = await expectOk(shell.call("passwords.list"), "passwords.list after remove");
      if (value.passwords.some((p) => p.username === `${username}-old`) || !value.passwords.some((p) => p.username === username)) throw new Error(JSON.stringify(value));
      const never = await expectOk(shell.call("passwords.neverSaveOrigins"), "passwords.neverSaveOrigins");
      if (!Array.isArray(never.origins)) throw new Error(JSON.stringify(never));
      return { remaining: value.passwords.length };
    });

    await t.check("secret verbs are refused on the generic route", async () => {
      return {
        reveal: await expectCode(shell.call("passwords.reveal", { origin: ORIGIN, username }), "ERR_SECRET_ROUTE", "generic reveal"),
        save: await expectCode(shell.call("passwords.save", { origin: ORIGIN, username, password }), "ERR_SECRET_ROUTE", "generic save"),
        card: await expectCode(shell.call("autofill.cardNumber", { id: "x" }), "ERR_SECRET_ROUTE", "generic card number"),
      };
    });

    await t.check("without system authentication nothing is revealed, changed or exported", async () => {
      const results = {
        reveal: await expectCode(shell.invoke("ArdiaPasswords.reveal", { args: ["", ORIGIN, username] }), "ERR_REAUTH_UNAVAILABLE", "reveal"),
        unlock: await expectCode(shell.invoke("ArdiaPasswords.unlock", { args: [""] }), "ERR_REAUTH_UNAVAILABLE", "unlock"),
        update: await expectCode(shell.invoke("ArdiaPasswords.update", { args: ["", ORIGIN, username, null, changed] }), "ERR_REAUTH_UNAVAILABLE", "update"),
        export: await expectCode(shell.invoke("ArdiaPasswords.export", { args: [""] }), "ERR_REAUTH_UNAVAILABLE", "export"),
      };
      return results;
    });

    let addressId;
    await t.check("addresses add, update and remove", async () => {
      addressId = (await expectOk(shell.call("autofill.saveAddress", { address: { name: `Ada ${run}`, city: "London", postalCode: "N1 9GU" } }), "saveAddress")).id;
      await expectOk(shell.call("autofill.saveAddress", { address: { id: addressId, name: `Ada ${run}`, city: "Cambridge" } }), "update address");
      const listed = await expectOk(shell.call("autofill.addresses"), "autofill.addresses");
      const entry = listed.addresses.find((a) => a.id === addressId);
      if (entry?.city !== "Cambridge") throw new Error(JSON.stringify(listed));
      const temporary = (await expectOk(shell.call("autofill.saveAddress", { address: { name: "Temporary" } }), "save temporary")).id;
      await expectOk(shell.call("autofill.remove", { id: temporary }), "autofill.remove");
      const after = await expectOk(shell.call("autofill.addresses"), "addresses after remove");
      if (after.addresses.some((a) => a.id === temporary)) throw new Error("the removed address is still listed");
      return { addressId, count: after.addresses.length };
    });

    let cardId;
    await t.check("a card saves through the typed method, lists only its last four digits and keeps its number gated", async () => {
      cardId = (await expectOk(shell.invoke("ArdiaAutofill.saveCard", { args: ["", { name: "Ada", expMonth: 4, expYear: 2031 }, card] }), "saveCard")).id;
      const listed = await expectOk(shell.call("autofill.cards"), "autofill.cards");
      const entry = listed.cards.find((c) => c.id === cardId);
      if (!entry || entry.last4 !== "1111" || JSON.stringify(listed).includes(card)) throw new Error(JSON.stringify(listed));
      await expectCode(shell.invoke("ArdiaAutofill.cardNumber", { args: ["", cardId] }), "ERR_REAUTH_UNAVAILABLE", "card number");
      return { cardId, network: entry.network };
    });

    await t.check("autofill and password toggles change", async () => {
      await expectOk(shell.call("passwords.setAutofillEnabled", { enabled: false }), "setAutofillEnabled");
      await expectOk(shell.call("autofill.setSettings", { addresses: false }), "setSettings");
      const enabled = await expectOk(shell.call("passwords.autofillEnabled"), "autofillEnabled");
      const settings = await expectOk(shell.call("autofill.settings"), "autofill.settings");
      if (enabled !== false || settings.addresses !== false || settings.cards !== true) throw new Error(JSON.stringify({ enabled, settings }));
      return { enabled, settings };
    });

    await t.check("Settings shows the honest notice for the basic store", async () => {
      await shell.command("openSettings");
      const settings = await state.app.shell("settings", { timeout: 20000 });
      await settings.clickText("Passwords", { timeout: 6000 });
      const text = await until(async () => {
        const body = await settings.text();
        return body.includes("basic password store") ? body : false;
      }, { timeout: 8000, label: "credential notice" });
      await t.screenshot(settings, "passwords-pane");
      if (!text.includes("System authentication is off in background instances")) throw new Error("the notice doesn't explain why authentication is unavailable");
      return { notice: true };
    });

    state = await restartBrowser(t, state, { name: "credentials" });

    await t.check("everything survives a restart", async () => {
      const shell2 = state.shell;
      const listed = await expectOk(shell2.call("passwords.list"), "passwords.list after restart");
      const addresses = await expectOk(shell2.call("autofill.addresses"), "addresses after restart");
      const cards = await expectOk(shell2.call("autofill.cards"), "cards after restart");
      const enabled = await expectOk(shell2.call("passwords.autofillEnabled"), "autofillEnabled after restart");
      const settings = await expectOk(shell2.call("autofill.settings"), "settings after restart");
      if (!listed.passwords.some((p) => p.username === username)) throw new Error("credential lost");
      if (!addresses.addresses.some((a) => a.id === addressId)) throw new Error("address lost");
      if (!cards.cards.some((c) => c.id === cardId)) throw new Error("card lost");
      if (enabled !== false || settings.addresses !== false) throw new Error(JSON.stringify({ enabled, settings }));
      return { passwords: listed.passwords.length, addresses: addresses.addresses.length, cards: cards.cards.length };
    }, { critical: true });

    await t.stop(state.app);

    await t.check("secrets are not stored or logged in plain text", async () => {
      const files = await storeFiles(state.scratch);
      if (!files.some((f) => f.name === "Login Data") || !files.some((f) => f.name === "Web Data")) throw new Error(`store files: ${files.map((f) => f.name)}`);
      const leaks = [];
      for (const { name, bytes } of files) {
        for (const secret of [password, changed, card]) if (bytes.includes(Buffer.from(secret))) leaks.push(`${secret.slice(0, 4)}… in ${name}`);
      }
      const log = await state.app.logText();
      for (const secret of [password, changed, card]) if (log.includes(secret)) leaks.push(`${secret.slice(0, 4)}… in the browser log`);
      if (leaks.length) throw new Error(leaks.join(", "));
      return { files: files.map((f) => f.name) };
    });
  },
};
