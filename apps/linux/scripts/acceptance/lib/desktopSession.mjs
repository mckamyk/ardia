import { mkdir } from "node:fs/promises";
import { join } from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { desktopTools, missingTools, startDesktop } from "./desktop.mjs";
import { startBrowser } from "./flows.mjs";
import { until } from "./util.mjs";

export const SESSION_TOOLS = { sway: ["sway", "swaymsg", "python", "xkbcli", "dbusDaemon"], x11: ["xvfb", "xdotool", "openbox", "xprop", "dbusDaemon"] };
const SESSION_KEYS = ["WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "DBUS_SYSTEM_BUS_ADDRESS", "XDG_CURRENT_DESKTOP", "XDG_SESSION_TYPE", "GTK_IM_MODULE", "QT_IM_MODULE", "XMODIFIERS"];
const OWNER_KEYS = ["SWAYSOCK", "I3SOCK", "HYPRLAND_INSTANCE_SIGNATURE", "XDG_SESSION_ID", "XDG_SEAT", "XDG_VTNR", "XAUTHORITY", "GNOME_KEYRING_CONTROL", "SSH_AUTH_SOCK"];
export const SOFTWARE_GL = ["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"];
let cachedTools = null;

export async function sessionTools() {
  cachedTools ??= desktopTools();
  return cachedTools;
}

export async function desktopUnavailable({ x11 = false, needs = [] } = {}) {
  const tools = await sessionTools();
  const missing = missingTools(tools, [...(x11 ? SESSION_TOOLS.x11 : SESSION_TOOLS.sway), ...needs]);
  if (missing.length) return `${missing.join(", ")} not found; install them (see "Desktop sessions" in docs/linux-parity/acceptance-suite.md)`;
  return null;
}

export async function openDesktop(t, { x11 = false, outputs = 1, name, ...options } = {}) {
  const tools = await sessionTools();
  const dir = await t.scratch(name ?? (x11 ? "x11" : "sway"));
  const desktop = await startDesktop(dir, { tools, outputs, x11, ...options });
  t.defer(() => desktop.stop());
  return desktop;
}

export function sessionEnvironment(desktop, extra = {}) {
  const env = {};
  for (const key of SESSION_KEYS) env[key] = desktop.env[key];
  for (const key of OWNER_KEYS) env[key] = undefined;
  if (desktop.kind === "x11") env.WAYLAND_DISPLAY = undefined;
  else env.DISPLAY = undefined;
  return { ...env, ...extra };
}

export function desktopLaunch(desktop, { env = {}, extraArgs = [], background = false } = {}) {
  return { background, ozone: desktop.kind === "x11" ? "x11" : "wayland", env: sessionEnvironment(desktop, env), extraArgs: [...SOFTWARE_GL, ...extraArgs] };
}

export async function startDesktopBrowser(t, desktop, { env, extraArgs, background, ...options } = {}) {
  return startBrowser(t, { ...options, ...desktopLaunch(desktop, { env, extraArgs, background }) });
}

export const hasFocus = (shell) => shell.eval("document.hasFocus()", { timeout: 3000 }).catch(() => false);

export function commandBarText(shell) {
  return shell.eval("(() => { const a = document.activeElement; const fields = [...document.querySelectorAll('input, textarea, [contenteditable=true]')].map((n) => n.value ?? n.textContent ?? ''); return { active: a ? (a.value ?? a.textContent ?? '') : null, tag: a?.tagName ?? null, fields: fields.filter(Boolean) }; })()", { timeout: 3000 });
}

export async function waitTyped(shell, text, { timeout = 8000 } = {}) {
  return until(async () => {
    const state = await commandBarText(shell);
    return state.active?.includes(text) ? state : false;
  }, { timeout, label: `"${text}" in the window's focused field` });
}

export async function waitFocused(desktop, test, { timeout = 10000, label = "the focused window" } = {}) {
  return until(async () => {
    const focused = await desktop.focused();
    return focused && test(focused) ? focused : false;
  }, { timeout, label }).catch(async (error) => {
    const windows = await desktop.windows().catch(() => []);
    throw new Error(`${error.message}; focused ${JSON.stringify((await desktop.focused().catch(() => null))?.title ?? null)}, windows ${JSON.stringify(windows.map((w) => ({ id: w.id, title: w.title, focused: w.focused, rect: w.rect })))}`);
  });
}

export async function newWindow(desktop, known, { timeout = 15000, label = "a new compositor window" } = {}) {
  return until(async () => (await desktop.windows()).find((w) => !known.some((k) => k.id === w.id)) ?? false, { timeout, label });
}

export async function clickShell(desktop, window, shell, selector) {
  const r = await until(() => shell.rect(selector), { timeout: 8000, label: `visible ${selector}` });
  await desktop.click(window.rect.x + r.x, window.rect.y + r.y);
  return r;
}

export async function desktopShot(t, desktop, name) {
  if (!desktop.screenshot) return null;
  await mkdir(t.evidenceDir, { recursive: true });
  return desktop.screenshot(join(t.evidenceDir, `${name}.png`)).catch(() => null);
}

export async function pointAt(desktop, window, rect) {
  await desktop.click(window.rect.x + rect.x, window.rect.y + rect.y);
  return rect;
}

export async function clickTextIn(desktop, window, shell, text, { exact = true, timeout = 8000 } = {}) {
  const r = await until(() => shell.rectByText(text, { exact }), { timeout, label: `visible text ${text}` });
  return pointAt(desktop, window, r);
}

export async function clickLabelIn(desktop, window, shell, label, { timeout = 8000 } = {}) {
  const r = await until(() => shell.eval(`(() => { const n = [...document.querySelectorAll('[aria-label],[title]')].find((n) => (n.getAttribute('aria-label') ?? n.getAttribute('title')) === ${JSON.stringify(label)} && n.getBoundingClientRect().width > 0); if (!n) return null; const r = n.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`), { timeout, label: `control labelled ${label}` });
  return pointAt(desktop, window, r);
}

export async function focusShellWindow(desktop, shell, window) {
  if (await hasFocus(shell)) return;
  if (desktop.kind === "x11") await desktop.raise(window).catch(() => {});
  const field = await shell.rect("input").catch(() => null);
  await pointAt(desktop, window, field ?? { x: 90, y: 600 });
  await until(() => hasFocus(shell), { timeout: 5000, label: "focus after clicking into the window" });
}

export async function openSettingsWindow(desktop, app, shell, { pane } = {}) {
  const from = await desktop.focused();
  if (!from) throw new Error("no window is focused in the compositor");
  await focusShellWindow(desktop, shell, from);
  const known = await desktop.windows();
  await desktop.key("ctrl+comma");
  const settings = await app.shell("settings", { timeout: 20000 });
  const window = await newWindow(desktop, known, { label: "the Settings window in the compositor" }).catch(async () => (await desktop.windows()).find((w) => /Settings/.test(w.title)));
  if (!window) throw new Error("no Settings window in the compositor");
  if (pane) await clickTextIn(desktop, window, settings, pane);
  return { settings, window };
}
