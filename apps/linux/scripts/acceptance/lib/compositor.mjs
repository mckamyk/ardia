import { spawn } from "node:child_process";
import { closeSync, openSync } from "node:fs";
import { access, chmod, mkdir, readdir, writeFile } from "node:fs/promises";
import { constants } from "node:fs";
import { delimiter, join } from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { until } from "./util.mjs";

export const COMPOSITORS = ["sway", "weston", "cage"];

export async function which(name, path = process.env.PATH ?? "") {
  for (const dir of path.split(delimiter).filter(Boolean)) {
    const candidate = join(dir, name);
    try {
      await access(candidate, constants.X_OK);
      return candidate;
    } catch {}
  }
  return null;
}

export async function findCompositor({ path, prefer = COMPOSITORS } = {}) {
  for (const name of prefer) {
    const found = await which(name, path);
    if (found) return { name, path: found };
  }
  return null;
}

export function compositorCommand(name, binary, { runtime, socket, config }) {
  if (name === "sway") return { command: binary, args: ["-c", config], env: { WLR_BACKENDS: "headless", WLR_LIBINPUT_NO_DEVICES: "1", WLR_RENDERER: "pixman", XDG_RUNTIME_DIR: runtime } };
  if (name === "weston") return { command: binary, args: ["--backend=headless", `--socket=${socket}`, "--idle-time=0"], env: { XDG_RUNTIME_DIR: runtime } };
  return null;
}

export async function startNestedCompositor(found, scratch) {
  const runtime = join(scratch, "runtime");
  await mkdir(runtime, { recursive: true });
  await chmod(runtime, 0o700);
  const socket = "ardia-acceptance-0";
  const config = join(scratch, "sway.conf");
  await writeFile(config, "output HEADLESS-1 resolution 1920x1080\nfocus_on_window_activation focus\n");
  const spec = compositorCommand(found.name, found.path, { runtime, socket, config });
  if (!spec) return { unsupported: `${found.name} runs one application at a time, so focus between windows cannot be judged` };
  const logPath = join(scratch, `${found.name}.log`);
  const log = openSync(logPath, "w");
  const base = { ...process.env };
  delete base.WAYLAND_DISPLAY;
  delete base.DISPLAY;
  delete base.SWAYSOCK;
  const child = spawn(spec.command, spec.args, { stdio: ["ignore", log, log], env: { ...base, ...spec.env }, detached: true });
  closeSync(log);
  let exited = false;
  child.once("exit", () => { exited = true; });
  child.once("error", () => { exited = true; });
  const display = await until(async () => {
    const names = await readdir(runtime);
    return names.find((name) => /^wayland-\d+$/.test(name) || name === socket) ?? false;
  }, { timeout: 15000, label: `${found.name} Wayland socket`, abort: () => (exited ? `${found.name} exited; log ${logPath}` : null) });
  const swaySocket = found.name === "sway" ? await until(async () => (await readdir(runtime)).find((n) => n.startsWith("sway-ipc.")) ?? false, { timeout: 5000, label: "sway IPC socket" }).catch(() => null) : null;
  return {
    name: found.name,
    runtime,
    display,
    logPath,
    env: { WAYLAND_DISPLAY: display, XDG_RUNTIME_DIR: runtime, ...(swaySocket ? { SWAYSOCK: join(runtime, swaySocket) } : {}) },
    async stop() {
      if (exited) return;
      try { process.kill(-child.pid, "SIGTERM"); } catch {}
      for (let i = 0; !exited && i < 60; i++) await delay(50);
      if (!exited) try { process.kill(-child.pid, "SIGKILL"); } catch {}
    },
  };
}
