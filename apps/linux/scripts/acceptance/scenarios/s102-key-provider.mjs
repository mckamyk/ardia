import { startBrowser } from "../lib/flows.mjs";

async function status(shell, method) {
  const reply = await shell.invoke(method, { args: [""] });
  if (!reply.ok) throw new Error(`${method} failed: ${reply.code} ${reply.message}`);
  return reply.value;
}

export default {
  id: "S102",
  issue: 102,
  title: "Status names the OSCrypt key that encrypts new secrets and flags a silent fallback to the v10 key",
  deadline: 180000,
  manual: [
    { name: "A working keyring is reported as the keyring", steps: ["Start Ardia with --password-store=gnome-libsecret in a session whose Secret Service is unlocked (or --password-store=kwallet6 under KDE).", "ArdiaPasswords.status reports protection \"keyring\", protectionFallback false and storage \"the Secret Service keyring\" (or \"KWallet\").", "Settings › Passwords shows 'Storage on this computer' naming that keyring and no warning."], reason: "needs a real Secret Service or KWallet session" },
    { name: "A keyring that stops answering is flagged", steps: ["Start Ardia with --password-store=gnome-libsecret and a locked or absent Secret Service.", "Settings › Passwords and Autofill say the keyring didn't answer and new secrets are only obfuscated."], reason: "the automated check covers an absent Secret Service; a locked one needs a desktop keyring" },
  ],
  async run(t) {
    const fallback = await startBrowser(t, { name: "keyring-fallback", extraArgs: ["--password-store=gnome-libsecret"] });
    await t.check("a configured keyring that doesn't answer reports the obfuscating v10 fallback", async () => {
      const passwords = await status(fallback.shell, "ArdiaPasswords.status");
      const autofill = await status(fallback.shell, "ArdiaAutofill.status");
      for (const value of [passwords, autofill]) {
        if (value.backend !== "gnome-libsecret" || value.protection !== "obfuscated" || value.protectionFallback !== true || !/built-in key/.test(value.storage ?? ""))
          throw new Error(JSON.stringify(value));
      }
      if (!passwords.passwords?.available || !autofill.cards?.available) throw new Error(`an obfuscated store must stay readable: ${JSON.stringify({ passwords, autofill })}`);
      return { passwords, autofill };
    }, { critical: true });
    await t.stop(fallback.app);

    const basic = await startBrowser(t, { name: "keyring-basic" });
    await t.check("the basic store reports obfuscation without calling it a fallback", async () => {
      const value = await status(basic.shell, "ArdiaPasswords.status");
      if (value.backend !== "basic" || value.protection !== "obfuscated" || value.protectionFallback !== false || !/basic store/.test(value.storage ?? ""))
        throw new Error(JSON.stringify(value));
      return value;
    });
  },
};
