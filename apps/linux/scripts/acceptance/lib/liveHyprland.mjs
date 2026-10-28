import { execFile } from "node:child_process";
import { join } from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { desktopTools, startDesktop } from "./desktop.mjs";
import { BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, openVirtualPointer } from "./waylandPointer.mjs";
import { until } from "./util.mjs";

export const LIVE_WORKSPACE = 9;
const BUTTONS = { left: BTN_LEFT, right: BTN_RIGHT, middle: BTN_MIDDLE };

export function liveRequest(env = process.env) {
  const signature = env.ARDIA_ACCEPTANCE_LIVE_HYPRLAND;
  if (!signature) return null;
  const uid = process.getuid?.() ?? 1000;
  return { signature, display: env.ARDIA_ACCEPTANCE_LIVE_WAYLAND ?? "wayland-1", runtime: env.ARDIA_ACCEPTANCE_LIVE_RUNTIME ?? `/run/user/${uid}` };
}

function hyprctl(request, args) {
  return new Promise((resolve, reject) => {
    execFile("hyprctl", args, { env: { PATH: process.env.PATH, HYPRLAND_INSTANCE_SIGNATURE: request.signature, XDG_RUNTIME_DIR: request.runtime }, timeout: 10000 }, (error, stdout) => (error ? reject(error) : resolve(String(stdout))));
  });
}

const json = async (request, what) => JSON.parse(await hyprctl(request, ["-j", what]));

export async function openLiveHyprland(t, request) {
  const tools = await desktopTools();
  const dir = await t.scratch("live-hyprland");
  const buses = await startDesktop(join(dir, "buses"), { tools, compositor: false });
  t.defer(() => buses.stop());
  const previous = (await json(request, "activeworkspace")).id;
  const occupied = (await json(request, "workspaces")).find((w) => w.id === LIVE_WORKSPACE && w.windows > 0);
  if (occupied) throw new Error(`workspace ${LIVE_WORKSPACE} already has ${occupied.windows} windows; refusing to share it`);
  const monitor = (await json(request, "monitors")).find((m) => m.focused) ?? (await json(request, "monitors"))[0];
  await hyprctl(request, ["dispatch", `hl.dsp.focus({ workspace = ${LIVE_WORKSPACE} })`]);
  t.defer(() => hyprctl(request, ["dispatch", `hl.dsp.focus({ workspace = ${Number(previous)} })`]).catch(() => {}));
  const live = {
    kind: "hyprland",
    dir,
    tools,
    home: buses.home,
    session: buses.session,
    system: buses.system,
    runtime: request.runtime,
    display: request.display,
    monitor: monitor.name,
    env: { ...buses.env, WAYLAND_DISPLAY: request.display, XDG_RUNTIME_DIR: request.runtime, XDG_CURRENT_DESKTOP: "Hyprland", XDG_SESSION_TYPE: "wayland" },
    run: buses.run,
    spawn: buses.spawn,
    async windows() {
      return (await json(request, "clients")).filter((c) => c.workspace?.id === LIVE_WORKSPACE).map((c) => ({ id: c.address, pid: c.pid, appId: c.class, title: c.title, focused: false, fullscreen: c.fullscreen > 0, fullscreenMode: c.fullscreen, floating: c.floating, output: c.monitor, workspace: c.workspace.id, rect: { x: c.at[0] - monitor.x, y: c.at[1] - monitor.y, width: c.size[0], height: c.size[1] } }));
    },
    async focused() {
      const active = await json(request, "activewindow").catch(() => null);
      if (!active?.address || active.workspace?.id !== LIVE_WORKSPACE) return null;
      const window = (await live.windows()).find((w) => w.id === active.address);
      return window ? { ...window, focused: true } : null;
    },
    async raise(window) {
      await hyprctl(request, ["dispatch", `hl.dsp.focus({ window = "address:${window.id}" })`]);
    },
    async dispatch(lua) {
      return hyprctl(request, ["dispatch", lua]);
    },
    async screenshot(file) {
      await buses.run(tools.grim, ["-o", monitor.name, file], { env: { WAYLAND_DISPLAY: request.display, XDG_RUNTIME_DIR: request.runtime }, timeout: 20000 });
      return file;
    },
  };
  const guard = async (what) => {
    const active = await json(request, "activeworkspace");
    if (active.id !== LIVE_WORKSPACE) throw new Error(`refusing to ${what}: workspace ${active.id} is active, not ${LIVE_WORKSPACE}`);
    const window = await live.focused();
    if (!window) throw new Error(`refusing to ${what}: no window on workspace ${LIVE_WORKSPACE} has focus`);
  };
  const keyboardEnv = { WAYLAND_DISPLAY: request.display, XDG_RUNTIME_DIR: request.runtime };
  const pointer = await openVirtualPointer({ runtime: request.runtime, display: request.display, width: monitor.width, height: monitor.height });
  t.defer(() => pointer.close());
  const pointInOwned = async (x, y) => (await live.windows()).some((w) => x >= w.rect.x && y >= w.rect.y && x < w.rect.x + w.rect.width && y < w.rect.y + w.rect.height);
  Object.assign(live, {
    async key(combo) {
      await guard(`press ${combo}`);
      await live.keyboard.send({ op: "combo", combo });
    },
    async type(text) {
      await guard("type");
      await live.keyboard.send({ op: "type", text, delay: 0.02 });
    },
    async tap(key) {
      await guard(`press ${key}`);
      await live.keyboard.send({ op: "tap", key });
    },
    async keymap(options = {}) {
      await live.keyboard.send({ op: "keymap", layout: options.layout ?? "us", variant: options.variant ?? "", options: options.options ?? "" });
    },
    async pointerMove(x, y) {
      await pointer.move(x + monitor.x, y + monitor.y);
    },
    async pointerPress(button = "left") {
      await pointer.button(BUTTONS[button] ?? button, true);
    },
    async pointerRelease(button = "left") {
      await pointer.button(BUTTONS[button] ?? button, false);
    },
    async click(x, y, button = "left") {
      if (!(await pointInOwned(x, y))) throw new Error(`refusing to click at ${x},${y}: no window this scenario started is there`);
      await live.pointerMove(x, y);
      await delay(80);
      await live.pointerPress(button);
      await delay(60);
      await live.pointerRelease(button);
    },
  });
  const { spawn } = await import("node:child_process");
  const script = new URL("./tools/virtual_keyboard.py", import.meta.url).pathname;
  const child = spawn("python3", ["-I", script, request.runtime, request.display], { env: { PATH: process.env.PATH, ...keyboardEnv }, stdio: ["pipe", "pipe", "pipe"], detached: true });
  t.defer(async () => {
    try { process.kill(-child.pid, "SIGTERM"); } catch {}
  });
  let buffer = "";
  const waiting = [];
  child.stdout.on("data", (chunk) => {
    buffer += chunk;
    let index;
    while ((index = buffer.indexOf("\n")) >= 0) {
      const line = buffer.slice(0, index);
      buffer = buffer.slice(index + 1);
      waiting.shift()?.(JSON.parse(line));
    }
  });
  const next = () => new Promise((resolve) => waiting.push(resolve));
  await Promise.race([next(), delay(10000).then(() => { throw new Error("the live virtual keyboard didn't start"); })]);
  let chain = Promise.resolve();
  live.keyboard = {
    send(request) {
      const result = chain.then(async () => {
        const reply = next();
        child.stdin.write(`${JSON.stringify(request)}\n`);
        const answer = await Promise.race([reply, delay(30000).then(() => ({ ok: false, error: "no answer in 30 s" }))]);
        if (!answer.ok) throw new Error(`virtual keyboard ${request.op}: ${answer.error}`);
      });
      chain = result.catch(() => {});
      return result;
    },
  };
  await until(async () => (await json(request, "activeworkspace")).id === LIVE_WORKSPACE, { timeout: 5000, label: `workspace ${LIVE_WORKSPACE}` });
  return live;
}
