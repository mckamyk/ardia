import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
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
    defaultBrowser: false, launchAtLogin: false, thinScrollBars: false, webkit: false, cliControl: false, externalLinks: false, performanceRecording: false, taskManager: false,
  });
  const full = linuxCapabilities(fakeHost(["ArdiaSystem.isDefaultBrowser", "ArdiaSystem.setAsDefaultBrowser", "ArdiaSystem.launchAtLoginStatus", "ArdiaSystem.setLaunchAtLogin", "ArdiaSystem.setThinScrollBars", "engine.beginTracing", "engine.endTracing", "engine.isTracing", "engine.openTaskManager", "ArdiaApp.openExternalURL"]).host);
  assert.deepEqual(full, { defaultBrowser: true, launchAtLogin: true, thinScrollBars: true, webkit: false, cliControl: false, externalLinks: true, performanceRecording: true, taskManager: true });
  assert.equal(linuxCapabilities(undefined).taskManager, false);
});

test("unavailable choices map to exact settings ids and the Engines pane", () => {
  const ids = ["general.defaultBrowser", "general.openAtLogin", "general.warnBeforeQuitting", "appearance.pageScrollBars", "engines.defaultEngine", "engines.protectedVideo", "search.otherEngines", "advanced.cliControl", "advanced.cliInstall", "advanced.showFullUrl"];
  const none = linuxCapabilities(fakeHost().host);
  assert.deepEqual([...unavailableSettingIds(none, ids)].sort(), ["advanced.cliControl", "advanced.cliInstall", "appearance.pageScrollBars", "engines.defaultEngine", "engines.protectedVideo", "general.defaultBrowser", "general.openAtLogin"]);
  assert.deepEqual([...unavailablePanes(none)], ["engines"]);
  assert.deepEqual([...unavailableSettingIds({ ...none, defaultBrowser: true, launchAtLogin: true, thinScrollBars: true, cliControl: true }, ids)].sort(), ["engines.defaultEngine", "engines.protectedVideo"]);
});

test("onboarding music and external links answer locally when the host has no backend for them", async () => {
  const { host, invoked } = fakeHost();
  const app = (method) => resolveHostModule(host, "ArdiaApp", method);
  assert.equal(await app("playIntroMusic")({}, false), undefined);
  assert.equal(await app("setIntroMusicMuted")(true), undefined);
  assert.equal(await app("stopIntroMusic")(0.6), undefined);
  assert.equal(await app("openExternalURL")("mailto:someone@example.com"), false);
  assert.deepEqual(invoked, []);
  const advertised = fakeHost(["ArdiaApp.openExternalURL"]);
  await assert.rejects(resolveHostModule(advertised.host, "ArdiaApp", "openExternalURL")("mailto:x"), /Unknown host method/);
  assert.deepEqual(advertised.invoked, ["ArdiaApp.openExternalURL"]);
});

const nativeSource = (path) => readFileSync(new URL(`../../../engine/ardiacore-linux/src/ardia/linux/${path}`, import.meta.url), "utf8");
const contractRoutes = () => {
  const source = nativeSource("ac_linux_contract.h");
  const table = source.slice(source.indexOf("kRoutes[] = {"), source.indexOf("};", source.indexOf("kRoutes[] = {")));
  return [...table.matchAll(/\{"([^"]+)",/g)].map((match) => match[1]);
};

test("the native bootstrap advertises every contract route, so engine capabilities can turn on", () => {
  const host = nativeSource("ac_linux_host.cc");
  const bootstrap = host.slice(host.indexOf("base::DictValue ShellHostView::Bootstrap() const {"), host.indexOf("void ShellHostView::LayoutOverlay()"));
  assert.match(bootstrap, /for \(const auto& route : ardiacore::linux_contract::kRoutes\)\s*capabilities\.Append\(route\.name\);/);
  const routes = contractRoutes();
  assert.ok(routes.includes("engine.openTaskManager"));
  const advertised = linuxCapabilities(fakeHost(routes).host);
  assert.equal(advertised.taskManager, true);
  assert.equal(advertised.performanceRecording, ["engine.beginTracing", "engine.endTracing", "engine.isTracing"].every((name) => routes.includes(name)));
});

test("the native bootstrap advertises the intercepted site zoom routes from the same list the dispatcher uses", () => {
  const host = nativeSource("ac_linux_host.cc");
  const bootstrap = host.slice(host.indexOf("base::DictValue ShellHostView::Bootstrap() const {"), host.indexOf("void ShellHostView::LayoutOverlay()"));
  assert.match(bootstrap, /for \(const char\* route : ardiacore::kLinuxZoomDomainRoutes\)\s*capabilities\.Append\(route\);/);
  assert.match(host, /if \(ardiacore::IsLinuxZoomDomainRoute\(name\)\) \{/);
  const zoom = nativeSource("ac_linux_history_zoom.h");
  const routes = [...zoom.slice(zoom.indexOf("kLinuxZoomDomainRoutes[]"), zoom.indexOf("};", zoom.indexOf("kLinuxZoomDomainRoutes[]"))).matchAll(/"([^"]+)"/g)].map((match) => match[1]);
  assert.deepEqual(routes, ["sites.zoomLevels", "sites.setZoom"]);
  for (const route of routes) assert.ok(!contractRoutes().includes(route));
});

test("Task Manager and tracing follow the advertised contract rather than the mere presence of a host", () => {
  assert.equal(linuxCapabilities(fakeHost().host).taskManager, false);
  assert.equal(linuxCapabilities(fakeHost(["engine.openTaskManager"]).host).taskManager, true);
  assert.equal(linuxCapabilities(fakeHost(["engine.beginTracing", "engine.endTracing", "engine.isTracing"]).host).performanceRecording, true);
});

test("accepting a site's notification prompt on Linux settles quietly when the desktop has no notification permission to ask", async () => {
  const { shellNativeModules } = await import("../../browser/src/test-native/index.ts");
  const { ensureNotificationPermission } = await import("./notificationPermission.ts");
  const app = shellNativeModules.ArdiaApp;
  const original = app.notificationPermission;
  const unhandled = [];
  const record = (reason) => unhandled.push(reason);
  process.on("unhandledRejection", record);
  app.notificationPermission = () => Promise.reject(Object.assign(new Error("Desktop notifications is unavailable on Linux"), { code: "ERR_UNAVAILABLE" }));
  try {
    assert.equal(await ensureNotificationPermission("main"), false);
    await new Promise((resolve) => setImmediate(resolve));
    assert.deepEqual(unhandled, []);
  } finally {
    app.notificationPermission = original;
    process.off("unhandledRejection", record);
  }
  assert.match(readFileSync(new URL("../vite.config.ts", import.meta.url), "utf8"), /here\("\.\.\/browser\/src\/state\/site\/webNotifications\.ts"\)\}\\0\$\{here\("\.\.\/browser\/src\/state\/site\/permissions\.ts"\)\}`, here\("\.\/src\/notificationPermission\.ts"\)/);
});
