import { mkdir } from "node:fs/promises";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { busCall, nameOwned } from "./desktopServices.mjs";
import { until } from "./util.mjs";

export const KEYRING_PASSWORD = "ardia-acceptance-keyring";
const LOGIN = "/org/freedesktop/secrets/collection/login";
const SECRET_ITEMS = fileURLToPath(new URL("./tools/secret_items.py", import.meta.url));

export async function secretItems(desktop, op, payload = {}) {
  const out = await desktop.run("python3", ["-I", SECRET_ITEMS, desktop.session.address, op, JSON.stringify(payload)], { env: { PYTHONDONTWRITEBYTECODE: "1" }, timeout: 40000 });
  return JSON.parse(out.trim() || "null");
}

export async function startKeyring(desktop, { state = "unlocked", items = [] } = {}) {
  await mkdir(join(desktop.home, ".local", "share", "keyrings"), { recursive: true });
  const proc = desktop.spawn(desktop.tools.keyring, ["--foreground", "--components=secrets", "--unlock"], { name: "gnome-keyring", stdio: ["pipe", "ignore", "ignore"], env: { GNOME_KEYRING_CONTROL: undefined } });
  proc.child.stdin.end(KEYRING_PASSWORD);
  await until(() => nameOwned(desktop, "org.freedesktop.secrets"), { timeout: 15000, interval: 200, label: "gnome-keyring-daemon on the private session bus", abort: () => (proc.state.exited ? `gnome-keyring-daemon exited (${proc.state.code})` : null) });
  const service = (method, args, signature) => busCall(desktop, { dest: "org.freedesktop.secrets", path: "/org/freedesktop/secrets", iface: "org.freedesktop.Secret.Service", method, args, signature });
  const locked = async () => busCall(desktop, { dest: "org.freedesktop.secrets", path: LOGIN, iface: "org.freedesktop.DBus.Properties", method: "Get", args: ["org.freedesktop.Secret.Collection", "Locked"], signature: "ss" });
  await until(async () => (await locked().catch(() => null)) === false, { timeout: 10000, label: "the login keyring to be created and unlocked" });
  for (const item of items) await secretItems(desktop, "create", item);
  if (state === "locked") {
    await service("Lock", [[LOGIN]], "ao");
    await until(async () => (await locked()) === true, { timeout: 10000, label: "the login keyring to lock" });
  }
  return {
    state,
    proc,
    locked,
    async lock() {
      await service("Lock", [[LOGIN]], "ao");
    },
    search: (attributes = {}, { secrets = false } = {}) => secretItems(desktop, "items", { attributes, secrets }),
    create: (item) => secretItems(desktop, "create", item),
    async unlock() {
      await secretItems(desktop, "unlock", { password: KEYRING_PASSWORD });
      await until(async () => (await locked()) === false, { timeout: 10000, label: "the login keyring to unlock" });
    },
    async items() {
      return busCall(desktop, { dest: "org.freedesktop.secrets", path: LOGIN, iface: "org.freedesktop.DBus.Properties", method: "Get", args: ["org.freedesktop.Secret.Collection", "Items"], signature: "ss" });
    },
    async stop() {
      try { process.kill(-proc.child.pid, "SIGTERM"); } catch {}
      await until(() => proc.state.exited, { timeout: 5000, label: "gnome-keyring-daemon to stop" }).catch(() => {});
    },
  };
}
