import assert from "node:assert/strict";
import test from "node:test";
import { appInfo, docs } from "../../browser/src/test-native/index.ts";
import { useBrowser } from "../../browser/src/state/browser/store.ts";
import { useOnboarding, onboardingCompletedAt } from "../../browser/src/state/onboarding/onboarding.ts";
import { useToasts } from "../../browser/src/state/splits/actions.ts";
import { APP_LAUNCH_LOCK, APP_LAUNCH_MARK, claimAppLaunch, runAppCommand, runLaunchServices, startAppCommands } from "./appServices.ts";

const capabilities = { defaultBrowser: false, launchAtLogin: false, thinScrollBars: false, webkit: false, cliControl: false, externalLinks: false, performanceRecording: false, taskManager: false };

function lockManager() {
  const held = new Set();
  const requests = [];
  return {
    held,
    requests,
    release: (name) => held.delete(name),
    request(name, options, callback) {
      requests.push({ name, options });
      if (held.has(name)) return Promise.resolve(callback(null));
      held.add(name);
      return Promise.resolve(callback({ name }));
    },
  };
}

function marks() {
  const values = new Map();
  return { values, getItem: (key) => values.get(key) ?? null, setItem: (key, value) => void values.set(key, value) };
}

function twoWindows() {
  useBrowser.setState(useBrowser.getInitialState(), true);
  useOnboarding.setState({ windowId: null, step: "intro" });
  docs.clear();
  const first = useBrowser.getState().createWindow({ url: "https://one.example/" });
  const second = useBrowser.getState().createWindow({ url: "https://two.example/" });
  return { first, second };
}

const toast = (windowId) => useToasts.getState().toasts[windowId];

test("app launch services run once across native windows and never in later or reloaded windows", async () => {
  const locks = lockManager();
  let runs = 0;
  const run = () => runs++;
  const owner = marks();
  assert.equal(await claimAppLaunch({ startup: true, locks, marks: owner, run }), "ran");
  assert.equal(await claimAppLaunch({ startup: true, locks, marks: marks(), run }), "heldElsewhere");
  assert.equal(await claimAppLaunch({ startup: false, locks, marks: marks(), run }), "notStartup");
  assert.equal(runs, 1);
  assert.deepEqual(locks.requests.map((r) => [r.name, r.options]), [[APP_LAUNCH_LOCK, { ifAvailable: true }], [APP_LAUNCH_LOCK, { ifAvailable: true }]]);
  assert.equal(owner.values.get(APP_LAUNCH_MARK), "1");
  locks.release(APP_LAUNCH_LOCK);
  assert.equal(await claimAppLaunch({ startup: true, locks, marks: owner, run }), "alreadyRan");
  assert.equal(runs, 1);
});

test("without an election primitive no window claims the app-wide services", async () => {
  let runs = 0;
  assert.equal(await claimAppLaunch({ startup: true, marks: marks(), run: () => runs++ }), "unavailable");
  const failing = { request: () => Promise.reject(new Error("locks unavailable")) };
  assert.equal(await claimAppLaunch({ startup: true, locks: failing, marks: marks(), run: () => runs++ }), "unavailable");
  assert.equal(runs, 0);
});

test("a launch task that throws still reports the claim and keeps the lease", async () => {
  const locks = lockManager();
  assert.equal(await claimAppLaunch({ startup: true, locks, marks: marks(), run: () => { throw new Error("boom"); } }), "ran");
  assert.ok(locks.held.has(APP_LAUNCH_LOCK));
});

test("a fresh profile starts onboarding once, in the owning window", () => {
  const { second } = twoWindows();
  appInfo.appVersion = "2.0";
  runLaunchServices(second, false, capabilities);
  assert.equal(useOnboarding.getState().windowId, second);
  const saved = JSON.parse(docs.get("onboarding.json"));
  assert.equal(saved.completedAt, null);
  assert.equal(typeof saved.startedAt, "number");
  assert.equal(saved.step, "intro");
  assert.equal(JSON.parse(docs.get("release-notes.json")).lastVersion, "2.0");
});

test("an existing Linux profile without onboarding history is marked complete instead of onboarded", () => {
  const { first } = twoWindows();
  runLaunchServices(first, true, capabilities);
  assert.equal(useOnboarding.getState().windowId, null);
  assert.equal(typeof onboardingCompletedAt(), "number");
});

test("completed onboarding stays finished and an interrupted one resumes in the owning window", () => {
  const { first, second } = twoWindows();
  runLaunchServices(first, false, capabilities);
  const started = docs.get("onboarding.json");
  useOnboarding.setState({ windowId: null });
  runLaunchServices(second, true, capabilities);
  assert.equal(useOnboarding.getState().windowId, second);
  assert.equal(onboardingCompletedAt(), null);
  assert.equal(JSON.parse(docs.get("onboarding.json")).startedAt, JSON.parse(started).startedAt);
  docs.set("onboarding.json", JSON.stringify({ completedAt: 1, startedAt: 1, version: 1 }));
  useOnboarding.setState({ windowId: null });
  runLaunchServices(first, false, capabilities);
  assert.equal(useOnboarding.getState().windowId, null);
});

test("app commands run only in the window that issued them", () => {
  const { first, second } = twoWindows();
  const listeners = [];
  const subscription = startAppCommands(first, capabilities, (listener) => (listeners.push(listener), { remove() {} }));
  assert.ok(subscription);
  listeners[0]({ command: "showOnboarding", windowId: second });
  assert.equal(useOnboarding.getState().windowId, null);
  listeners[0]({ command: "showOnboarding" });
  assert.equal(useOnboarding.getState().windowId, null);
  listeners[0]({ command: "showOnboarding", windowId: first });
  assert.equal(useOnboarding.getState().windowId, first);
});

test("unavailable help and developer commands say so instead of failing silently", () => {
  const { first } = twoWindows();
  for (const command of ["taskManager", "recordPerformanceIssue", "releaseNotes", "videoTour"]) {
    useToasts.setState({ toasts: {} });
    assert.equal(runAppCommand(first, command, capabilities), true, command);
    assert.equal(toast(first)?.title, "Not available on Linux yet", command);
  }
  assert.equal(runAppCommand(first, "newTab", capabilities), false);
});

test("Send Feedback copies the report and says where it went when no mail app can open", async () => {
  const { first } = twoWindows();
  useToasts.setState({ toasts: {} });
  runAppCommand(first, "sendFeedback", capabilities);
  for (let i = 0; i < 20 && !toast(first); i++) await new Promise((resolve) => setTimeout(resolve, 5));
  assert.equal(toast(first)?.title, "Feedback report copied");
});
