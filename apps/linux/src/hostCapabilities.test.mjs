import assert from "node:assert/strict";
import test from "node:test";
import { resolveHostModule } from "./hostModules.ts";
import { linuxCapabilities, unavailablePanes, unavailableSettingIds } from "./hostCapabilities.ts";

function fakeHost(capabilities = []) {
  const invoked = [];
  return {
    invoked,
    host: {
      bootstrap: { windowId: "w1", profileId: "default", capabilities },
      invoke: async (method, args) => { invoked.push(method); throw new Error(`Unknown host method ${method}`); },
      call: async () => "{}",
      onEvent: () => () => {},
    },
  };
}

test("absent default-browser, login and scrollbar backends answer truthfully without reaching the host", async () => {
  const { host, invoked } = fakeHost();
  const system = (method) => resolveHostModule(host, "ArdiaSystem", method);
  await assert.rejects(system("isDefaultBrowser")(), { code: "ERR_UNAVAILABLE" });
  await assert.rejects(system("setAsDefaultBrowser")(), { code: "ERR_UNAVAILABLE" });
  assert.equal(await system("launchAtLoginStatus")(), "notFound");
  await assert.rejects(system("setLaunchAtLogin")(true), { code: "ERR_UNAVAILABLE" });
  assert.equal(await system("setThinScrollBars")(true), undefined);
  assert.deepEqual(invoked, []);
});

test("a host that advertises a system backend is called through instead of being masked", async () => {
  const { host, invoked } = fakeHost(["ArdiaSystem.isDefaultBrowser", "ArdiaSystem.setAsDefaultBrowser", "ArdiaSystem.setThinScrollBars"]);
  await assert.rejects(resolveHostModule(host, "ArdiaSystem", "isDefaultBrowser")(), /Unknown host method/);
  await assert.rejects(resolveHostModule(host, "ArdiaSystem", "setThinScrollBars")(true), /Unknown host method/);
  assert.deepEqual(invoked, ["ArdiaSystem.isDefaultBrowser", "ArdiaSystem.setThinScrollBars"]);
});

test("Linux capabilities come from the host contract, and WebKit is never offered", () => {
  assert.deepEqual(linuxCapabilities(fakeHost().host), {
    defaultBrowser: false, launchAtLogin: false, thinScrollBars: false, webkit: false, performanceRecording: false, taskManager: true,
  });
  const full = linuxCapabilities(fakeHost(["ArdiaSystem.isDefaultBrowser", "ArdiaSystem.setAsDefaultBrowser", "ArdiaSystem.launchAtLoginStatus", "ArdiaSystem.setLaunchAtLogin", "ArdiaSystem.setThinScrollBars", "engine.beginTracing", "engine.endTracing", "engine.isTracing"]).host);
  assert.deepEqual(full, { defaultBrowser: true, launchAtLogin: true, thinScrollBars: true, webkit: false, performanceRecording: true, taskManager: true });
  assert.equal(linuxCapabilities(undefined).taskManager, false);
});

test("unavailable choices map to exact settings ids and the Engines pane", () => {
  const ids = ["general.defaultBrowser", "general.openAtLogin", "general.warnBeforeQuitting", "appearance.pageScrollBars", "engines.defaultEngine", "engines.protectedVideo", "search.otherEngines"];
  const none = linuxCapabilities(fakeHost().host);
  assert.deepEqual([...unavailableSettingIds(none, ids)].sort(), ["appearance.pageScrollBars", "engines.defaultEngine", "engines.protectedVideo", "general.defaultBrowser", "general.openAtLogin"]);
  assert.deepEqual([...unavailablePanes(none)], ["engines"]);
  assert.deepEqual([...unavailableSettingIds({ ...none, defaultBrowser: true, launchAtLogin: true, thinScrollBars: true }, ids)].sort(), ["engines.defaultEngine", "engines.protectedVideo"]);
});
