import { readFile } from "node:fs/promises";
import { sessionPath, sessionSeed, writeChromePreferences, writeProfileSeed } from "./seed.mjs";
import { until } from "./util.mjs";

export async function startBrowser(t, { name = "profile", scratch, seed = true, session, onboardingComplete = true, preferences, background = true, extraArgs, env, ozone, label } = {}) {
  const dir = scratch ?? (await t.scratch(name));
  if (!scratch) {
    await writeProfileSeed(dir, { session: seed ? (session ?? sessionSeed()) : undefined, onboardingComplete });
    if (preferences) await writeChromePreferences(dir, preferences);
  }
  const app = await t.launch({ scratch: dir, background, extraArgs, env, ozone, label: label ?? name });
  const shell = await app.mainShell();
  return { app, shell, scratch: dir };
}

export async function restartBrowser(t, current, options = {}) {
  await t.stop(current.app);
  return startBrowser(t, { ...options, scratch: current.scratch, label: options.label ?? "restart" });
}

export async function navigateTab(shell, tabId, url, { timeout = 25000, match = (current) => current === url } = {}) {
  await shell.navigate(tabId, url);
  await shell.waitTabUrl(tabId, match, { timeout, label: `tab ${tabId} to commit ${url}` });
}

export async function readSavedSession(scratch) {
  const text = await readFile(sessionPath(scratch), "utf8").catch(() => null);
  if (!text) return null;
  try { return JSON.parse(text); } catch { return null; }
}

export async function waitSavedSession(scratch, test, { timeout = 15000, label = "saved session" } = {}) {
  return until(async () => {
    const session = await readSavedSession(scratch);
    return session && test(session) ? session : false;
  }, { timeout, label });
}
