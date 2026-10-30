import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { evaluate } from "../lib/cdp.mjs";
import { restartBrowser, startBrowser } from "../lib/flows.mjs";
import { js, nonce, until } from "../lib/util.mjs";
import { FIXTURE_NAME, prepareExtension } from "./f09-extensions.mjs";

const COOKIE = "ardia_w70";
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const writeMarks = (session, value) => evaluate(session, `(() => { document.cookie = ${js(`${COOKIE}=${value}; max-age=3600; path=/`)}; localStorage.setItem('w70', ${js(value)}); return true; })()`);
const readMarks = (session) => evaluate(session, `({ cookie: document.cookie.split('; ').find(c => c.startsWith(${js(`${COOKIE}=`)}))?.split('=')[1] ?? null, storage: localStorage.getItem('w70') })`);

async function pageIn(app, url) {
  const target = await app.pageLoaded(url, { timeout: 30000 });
  const info = (await app.browserTargets()).find((candidate) => candidate.targetId === target.id);
  const { windowId } = await app.browser.send("Browser.getWindowForTarget", { targetId: target.id });
  return { target, session: await app.connect(target), context: info?.browserContextId ?? null, nativeWindow: windowId };
}

async function openTab(app, shell, url, profileId) {
  const tabId = await shell.state(`return s.newTab(windowId, { url: ${js(url)}${profileId ? `, profileId: ${js(profileId)}` : ""} });`);
  if (typeof tabId !== "string") throw new Error(`newTab returned ${JSON.stringify(tabId)}`);
  await shell.waitTabUrl(tabId, (current) => current === url, { timeout: 30000, label: `${url} in ${shell.windowId}` });
  return { tabId, ...(await pageIn(app, url)) };
}

async function switchTo(shell, profileId) {
  await shell.state(`s.switchProfile(windowId, ${js(profileId)}); return true;`);
  return shell.waitState(`return s.windows[windowId]?.profileId === ${js(profileId)} || null;`, { label: `the window on ${profileId}` });
}

async function windowGone(app, nativeWindow) {
  return until(async () => {
    const reply = await app.browser.send("Browser.getWindowBounds", { windowId: nativeWindow }).then(() => true, () => false);
    return !reply;
  }, { timeout: 20000, label: `native browser window ${nativeWindow} to close` });
}

export const w70 = {
  id: "W70",
  issue: 70,
  title: "Every regular profile pages in place in one window, each tab in its own engine profile, with extensions kept per engine profile",
  deadline: 900000,
  manual: [
    { name: "Swiping toward an isolated profile", steps: ["Create an isolated profile (its own data) and a shared-data profile.", "In one window, swipe with two fingers toward each: the sidebar pages in place to that profile's tabs and no other window appears."], reason: "needs trackpad gestures" },
  ],
  async run(t) {
    await paging(t);
    await extensionsPerProfile(t);
  },
};

async function paging(t) {
  const run = nonce("w70");
  let state = await startBrowser(t, { name: "paging" });
  const url = (label) => `${t.fixtures.url.isolation("isolation.ardia.test")}?p=${label}-${run}`;
  let profiles;
  let home;
  let windows;
  await t.check("the default profile's page stores its own cookie and localStorage", async () => {
    profiles = await state.shell.state("return { work: s.createProfile({ name: 'Work' }), side: s.createProfile({ name: 'Side', shareWith: 'default' }) };");
    home = await openTab(state.app, state.shell, url("default"));
    await writeMarks(home.session, `default-${run}`);
    windows = await state.shell.windowIds();
    return { profiles, context: home.context, nativeWindow: home.nativeWindow, windows };
  }, { critical: true });

  let isolated;
  await t.check("switching to an isolated profile pages in place: no window opens and its tab runs in that profile's own browser context", async () => {
    await switchTo(state.shell, profiles.work);
    isolated = await openTab(state.app, state.shell, url("isolated"));
    const after = await state.shell.windowIds();
    if (JSON.stringify(after) !== JSON.stringify(windows)) throw new Error(`native windows went from ${JSON.stringify(windows)} to ${JSON.stringify(after)}`);
    if (!isolated.context || isolated.context === home.context) throw new Error(`the isolated tab runs in ${isolated.context}, the default tab in ${home.context}`);
    if (isolated.nativeWindow === home.nativeWindow) throw new Error("the isolated tab sits in the default profile's Browser");
    const tab = await state.shell.tab(isolated.tabId);
    if (tab.windowId !== state.shell.windowId || tab.profileId !== profiles.work) throw new Error(`the isolated tab is ${JSON.stringify({ windowId: tab.windowId, profileId: tab.profileId })}`);
    return { windows: after, context: isolated.context, companion: isolated.nativeWindow };
  }, { critical: true });

  await t.check("the isolated profile sees none of the default profile's cookies or storage, and its own writes stay there", async () => {
    const marks = await readMarks(isolated.session);
    if (marks.cookie || marks.storage) throw new Error(`the isolated page reads ${JSON.stringify(marks)}`);
    await writeMarks(isolated.session, `isolated-${run}`);
    const back = await readMarks(home.session);
    if (back.cookie !== `default-${run}` || back.storage !== `default-${run}`) throw new Error(`the default page now reads ${JSON.stringify(back)}`);
    return { isolated: await readMarks(isolated.session), default: back };
  });

  await t.check("switching back shows the default profile's tab, still in its own context", async () => {
    await switchTo(state.shell, "default");
    const marks = await readMarks(home.session);
    if (marks.cookie !== `default-${run}`) throw new Error(`the default page reads ${JSON.stringify(marks)}`);
    const after = await state.shell.windowIds();
    if (after.length !== windows.length) throw new Error(`native windows ${JSON.stringify(after)}`);
    return { marks };
  });

  await t.check("a profile sharing the default profile's data pages in place into the same Browser and context", async () => {
    await switchTo(state.shell, profiles.side);
    const shared = await openTab(state.app, state.shell, url("shared"));
    const marks = await readMarks(shared.session);
    if (shared.context !== home.context) throw new Error(`the shared profile runs in ${shared.context}`);
    if (shared.nativeWindow !== home.nativeWindow) throw new Error("a profile sharing the default data got a Browser of its own");
    if (marks.cookie !== `default-${run}`) throw new Error(`the shared profile reads ${JSON.stringify(marks)}`);
    await switchTo(state.shell, "default");
    return { context: shared.context, marks };
  });

  await t.check("moving a tab to the isolated profile keeps it in this window and reloads it in the isolated context", async () => {
    const moving = await openTab(state.app, state.shell, url("move"));
    if (moving.context !== home.context) throw new Error(`the tab to move started in ${moving.context}`);
    await state.shell.state(`s.moveTabToProfile(${js(moving.tabId)}, ${js(profiles.work)}); return true;`);
    const moved = await until(async () => {
      const targets = (await state.app.browserTargets()).filter((target) => target.type === "page" && target.url === url("move"));
      return targets.length === 1 && targets[0].browserContextId === isolated.context ? targets[0] : false;
    }, { timeout: 30000, label: "the moved page to run only in the isolated context" });
    const tab = await state.shell.tab(moving.tabId);
    if (!tab || tab.windowId !== state.shell.windowId || tab.profileId !== profiles.work) throw new Error(`the moved tab is ${JSON.stringify(tab && { windowId: tab.windowId, profileId: tab.profileId })}`);
    const after = await state.shell.windowIds();
    if (after.length !== windows.length) throw new Error(`native windows ${JSON.stringify(after)}`);
    return { target: moved.targetId, context: moved.browserContextId };
  });

  await t.check("closing a window that holds tabs of two engine profiles closes its hidden isolated-profile Browser too", async () => {
    const second = await state.shell.state("return s.createWindow({});");
    await until(async () => (await state.shell.windowIds()).includes(second), { timeout: 20000, label: "the second native window" });
    const other = await state.app.shell(second, { timeout: 25000 });
    await openTab(state.app, other, url("second-default"));
    await switchTo(other, profiles.work);
    const work = await openTab(state.app, other, url("second-isolated"));
    if (work.nativeWindow === isolated.nativeWindow) throw new Error("two windows share one isolated-profile Browser");
    await other.state("s.closeWindow(windowId); return true;").catch(() => {});
    await until(async () => !(await state.shell.windowIds()).includes(second), { timeout: 20000, label: "the second window to close" });
    await windowGone(state.app, work.nativeWindow);
    const left = (await state.app.browserTargets()).filter((target) => target.type === "page" && target.url.includes(`second-`));
    if (left.length) throw new Error(`pages of the closed window survive: ${left.map((target) => target.url).join(", ")}`);
    const still = await readMarks(isolated.session);
    if (still.cookie !== `isolated-${run}`) throw new Error(`the first window's isolated tab reads ${JSON.stringify(still)}`);
    return { closedCompanion: work.nativeWindow };
  });

  await t.check("after a restart the window restores both profiles' tabs, each in its own profile", async () => {
    await delay(2500);
    state = await restartBrowser(t, state);
    const tabs = await state.shell.state(`const w = s.windows[windowId]; return (w?.tabIds ?? []).map((id) => ({ id, url: s.tabs[id]?.url, profileId: s.tabs[id]?.profileId }));`);
    const restored = tabs.find((tab) => tab.url === url("isolated"));
    if (!restored || restored.profileId !== profiles.work) throw new Error(`restored tabs ${JSON.stringify(tabs)}`);
    if (!tabs.some((tab) => tab.url === url("default") && tab.profileId === "default")) throw new Error(`the default tab is missing from ${JSON.stringify(tabs)}`);
    await switchTo(state.shell, profiles.work);
    await state.shell.state(`s.activate(${js(restored.id)}); return true;`);
    const page = await pageIn(state.app, url("isolated"));
    const marks = await readMarks(page.session);
    const regular = (await state.app.browserTargets()).find((target) => target.type === "page" && target.url === url("default"));
    if (regular && regular.browserContextId === page.context) throw new Error("after restart the isolated tab runs in the default context");
    if (marks.cookie !== `isolated-${run}`) throw new Error(`the restored isolated tab reads ${JSON.stringify(marks)}`);
    return { tabs: tabs.length, context: page.context, marks };
  });
  await t.stop(state.app);
}

async function contentScriptFixture(dir) {
  await prepareExtension(dir);
  const manifest = JSON.parse(await readFile(join(dir, "manifest.json"), "utf8"));
  manifest.content_scripts = [{ matches: ["http://isolation.ardia.test/*"], js: ["mark.js"], run_at: "document_end" }];
  manifest.host_permissions = ["http://isolation.ardia.test/*"];
  await writeFile(join(dir, "manifest.json"), JSON.stringify(manifest, null, 2));
  await writeFile(join(dir, "mark.js"), "document.documentElement.dataset.ardiaFixture = chrome.runtime.id;\n");
  return dir;
}

async function installed(shell, path, profile) {
  const reply = await shell.call("extensions.install", { path }, profile);
  if (!reply.ok) throw new Error(`installing into ${JSON.stringify(profile)} failed: ${reply.code} ${reply.message}`);
  return reply.value.id;
}

async function workerOf(app, id, context) {
  const target = await until(async () => {
    const infos = await app.browserTargets();
    const info = infos.find((candidate) => candidate.type === "service_worker" && candidate.url.startsWith(`chrome-extension://${id}/`) &&
      (!context || candidate.browserContextId === context));
    if (!info) return false;
    return (await app.targets()).find((candidate) => candidate.id === info.targetId) ?? false;
  }, { timeout: 30000, label: `the service worker of ${id}` });
  return app.connect(target);
}

const marked = (session) => evaluate(session, "document.documentElement.dataset.ardiaFixture ?? null");

async function extensionsPerProfile(t) {
  const run = nonce("w70e");
  const state = await startBrowser(t, { name: "paging-extensions", extensions: true });
  const { app, shell } = state;
  const url = (label) => `${t.fixtures.url.isolation("isolation.ardia.test")}?p=${label}-${run}`;
  const fixture = await contentScriptFixture(await t.scratch("profile-fixture"));
  let profiles;
  let pages;
  let workId;
  await t.check("an extension installed only in the isolated profile runs on its pages and nowhere else", async () => {
    profiles = await shell.state("return { work: s.createProfile({ name: 'Work' }), side: s.createProfile({ name: 'Side', shareWith: 'default' }) };");
    const home = await openTab(app, shell, url("home"));
    await switchTo(shell, profiles.work);
    const first = await openTab(app, shell, url("work-first"));
    workId = await installed(shell, fixture, profiles.work);
    const work = await openTab(app, shell, url("work"));
    await switchTo(shell, "default");
    const other = await openTab(app, shell, url("other"));
    pages = { home, work, other, first };
    const ran = await until(() => marked(work.session), { timeout: 15000, label: "the content script on the isolated page" });
    const leaked = { home: await marked(home.session), other: await marked(other.session) };
    if (leaked.home || leaked.other) throw new Error(`the isolated profile's content script ran on default pages: ${JSON.stringify(leaked)}`);
    const regularList = await shell.call("extensions.list", {});
    if ((regularList.value?.extensions ?? []).some((x) => x.name === FIXTURE_NAME)) throw new Error("the default profile lists the isolated profile's extension");
    return { workId, ran, leaked };
  }, { critical: true });

  await t.check("the toolbar follows the window's engine profile", async () => {
    const pinned = await shell.call("extensions.configure", { id: workId, options: { pinned: true } }, profiles.work);
    if (!pinned.ok) throw new Error(`pinning failed: ${pinned.code} ${pinned.message}`);
    await switchTo(shell, profiles.work);
    const shown = await until(async () => (await shell.labels()).includes("Fixture Action") || false, { timeout: 15000, label: "the isolated profile's action on the toolbar" });
    await switchTo(shell, "default");
    await delay(1500);
    if ((await shell.labels()).includes("Fixture Action")) throw new Error("the default profile's toolbar shows the isolated profile's extension");
    await switchTo(shell, profiles.side);
    await delay(1000);
    if ((await shell.labels()).includes("Fixture Action")) throw new Error("a profile sharing the default data shows the isolated profile's extension");
    await switchTo(shell, "default");
    return { shown };
  });

  await t.check("the isolated profile's extension queries only its own profile's tabs, and its current window never surfaces a hidden window", async () => {
    const windowsBefore = await shell.windowIds();
    const sw = await workerOf(app, workId);
    const all = await evaluate(sw, "chrome.tabs.query({}).then((tabs) => tabs.map((tab) => tab.url))");
    const foreign = all.filter((tabUrl) => tabUrl.includes(`home-${run}`) || tabUrl.includes(`other-${run}`));
    if (foreign.length) throw new Error(`the isolated extension sees default tabs: ${foreign.join(", ")}`);
    if (!all.some((tabUrl) => tabUrl.includes(`work-${run}`))) throw new Error(`the isolated extension misses its own tab: ${JSON.stringify(all)}`);
    await switchTo(shell, profiles.work);
    const current = await evaluate(sw, "chrome.windows.getLastFocused({ populate: true }).then((w) => ({ id: w.id, urls: w.tabs.map((tab) => tab.url) })).catch((error) => ({ error: String(error) }))");
    const currentTabs = await evaluate(sw, "chrome.tabs.query({ lastFocusedWindow: true }).then((tabs) => tabs.map((tab) => tab.url))");
    if (current.error) throw new Error(`windows.getLastFocused failed: ${current.error}`);
    if (current.urls.some((tabUrl) => tabUrl.includes(`home-${run}`) || tabUrl.includes(`other-${run}`))) throw new Error(`the extension's window lists default tabs: ${JSON.stringify(current)}`);
    if (currentTabs.some((tabUrl) => tabUrl.includes(`home-${run}`) || tabUrl.includes(`other-${run}`))) throw new Error(`lastFocusedWindow tabs include default tabs: ${JSON.stringify(currentTabs)}`);
    await evaluate(sw, `chrome.windows.update(${current.id}, { focused: true, drawAttention: true }).then(() => true).catch(() => false)`);
    await evaluate(sw, `chrome.tabs.create({ windowId: ${current.id}, url: ${js(url("from-extension"))}, active: false }).then(() => true)`);
    const adopted = await shell.waitState(`return Object.values(s.tabs).find((tab) => tab.url === ${js(url("from-extension"))}) ?? null;`, { timeout: 20000, label: "the extension's tab in this window" });
    if (adopted.windowId !== shell.windowId || adopted.profileId !== profiles.work) throw new Error(`the extension's tab landed as ${JSON.stringify({ windowId: adopted.windowId, profileId: adopted.profileId })}`);
    const windowsAfter = await shell.windowIds();
    if (JSON.stringify(windowsAfter) !== JSON.stringify(windowsBefore)) throw new Error(`windows changed from ${JSON.stringify(windowsBefore)} to ${JSON.stringify(windowsAfter)}`);
    const { bounds } = await app.browser.send("Browser.getWindowBounds", { windowId: current.id });
    await switchTo(shell, "default");
    return { tabs: all.length, current: current.id, companionState: bounds.windowState, adopted: adopted.id };
  });

  await t.check("an extension of the default profile is shared by the profile sharing its data and sees both profiles' tabs, with no Browser of their own", async () => {
    const copy = await contentScriptFixture(await t.scratch("shared-fixture"));
    const sharedId = await installed(shell, copy, "");
    await switchTo(shell, profiles.side);
    const shared = await openTab(app, shell, url("side"));
    if (shared.nativeWindow !== pages.home.nativeWindow) throw new Error("the sharing profile's tab got a Browser of its own");
    const sw = await workerOf(app, sharedId, pages.home.context);
    const all = await evaluate(sw, "chrome.tabs.query({}).then((tabs) => tabs.map((tab) => tab.url))");
    for (const label of ["home", "other", "side"]) {
      if (!all.some((tabUrl) => tabUrl.includes(`${label}-${run}`))) throw new Error(`the shared extension misses the ${label} tab: ${JSON.stringify(all)}`);
    }
    if (all.some((tabUrl) => tabUrl.includes(`work-${run}`))) throw new Error(`the shared extension sees the isolated profile's tab: ${JSON.stringify(all)}`);
    const ran = await until(() => marked(shared.session), { timeout: 15000, label: "the shared extension's content script on the sharing profile's page" });
    if (ran !== sharedId) throw new Error(`the sharing profile's page was marked by ${ran}`);
    await switchTo(shell, "default");
    return { sharedId, tabs: all.length };
  });
  await t.stop(app);
}
