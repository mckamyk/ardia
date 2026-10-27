import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { HostPageEvents } from "./hostPageEvents.ts";
import { applyHostPageTheme, resetHostPage } from "./hostPageLifecycle.ts";
import { useBrowser } from "../../browser/src/state/browser/store.ts";
import { pageOf, patchPage } from "../../browser/src/state/tabs/pageState.ts";
import { navigated } from "../../browser/src/state/tabs/actions.ts";

const lease = { claim: "claim", rendererGeneration: "2", bindingGeneration: "3", documentEpoch: "1" };
const navigation = { url: "https://example.test", committedUrl: "https://example.test", title: "Example", canGoBack: false, canGoForward: false, isLoading: false, themeColor: null, themeColorSource: null };
const security = { level: "secure", url: "https://example.test", origin: "https://example.test" };
const event = (name, data) => ({ name, data });
const message = (sequence, name, data, overrides = {}) => ({ ...lease, type: "pageEvent", sequence, event: event(name, data), ...overrides });
const control = (sequence, documentEpoch, extra = {}) => ({ ...lease, type: "pageLifecycle", sequence, documentEpoch, action: "reset", reason: "document", ...extra });
const popup = (id, url = `https://pop.test/${id}`) => event("popupBlocked", { id, url, origin: "https://example.test" });
const status = ({ fullscreen = false, text = "", hung = false, crashed, popups = [] } = {}) => [
  event("navigation", navigation), event("security", security),
  event("fullscreen", { fullscreen }), event("status", { text }),
  ...(crashed ? [event("crashed", crashed)] : []),
  event(hung ? "unresponsive" : "responsive", {}),
  ...popups.map((id) => popup(id)),
];

function setup(events = status()) {
  useBrowser.getState().hydrate({});
  const window = useBrowser.getState().createWindow({ url: navigation.url });
  const tab = useBrowser.getState().windows[window].tabIds[0];
  const props = {
    onNavigationChange: (value) => navigated(tab, { url: value.url, title: value.title }, value),
    onSecurity: (value) => patchPage(tab, { security: value }),
    onFullscreen: (fullscreen) => patchPage(tab, { fullscreen }),
    onStatus: (text) => patchPage(tab, { status: text }),
    onCrashed: (crashed) => patchPage(tab, { crashed, unresponsive: false, fullscreen: false, status: "" }),
    onUnresponsive: () => patchPage(tab, { unresponsive: true }),
    onResponsive: () => patchPage(tab, { unresponsive: false }),
    onPopupBlocked: (value) => patchPage(tab, { popups: [...pageOf(tab).popups, value] }),
  };
  const receiver = new HostPageEvents("claim", () => 7, () => props, (reason, replaced) => resetHostPage(tab, reason, replaced), (state) => applyHostPageTheme(tab, state));
  assert.ok(receiver.install({ lease, snapshot: { watermark: 1, events } }));
  const send = (value) => receiver.receive("ArdiaWebView", "7", value);
  return { tab, send, page: () => pageOf(tab) };
}

test("a creation snapshot replays fullscreen, status, crash, hung and every blocked pop-up", () => {
  const crashed = { status: 139, reason: "crashed", code: 139 };
  const h = setup(status({ fullscreen: true, text: "https://example.test/link", hung: true, crashed, popups: ["4", "5"] }));
  const page = h.page();
  assert.equal(page.fullscreen, false);
  assert.deepEqual(page.crashed, crashed);
  assert.equal(page.unresponsive, true);
  assert.equal(page.status, "");
  assert.deepEqual(page.popups.map((p) => p.id), ["4", "5"]);
  const live = setup(status({ fullscreen: true, text: "https://example.test/link", popups: ["9"] }));
  assert.equal(live.page().fullscreen, true);
  assert.equal(live.page().status, "https://example.test/link");
});

test("live producers update page state and hovered links clear with an empty status", () => {
  const h = setup();
  assert.ok(h.send(message(2, "fullscreen", { fullscreen: true })));
  assert.equal(h.page().fullscreen, true);
  assert.ok(h.send(message(3, "fullscreen", { fullscreen: false })));
  assert.equal(h.page().fullscreen, false);
  assert.ok(h.send(message(4, "status", { text: "https://example.test/a" })));
  assert.equal(h.page().status, "https://example.test/a");
  assert.ok(h.send(message(5, "status", { text: "" })));
  assert.equal(h.page().status, "");
  assert.ok(h.send(message(6, "unresponsive", {})));
  assert.equal(h.page().unresponsive, true);
  assert.ok(h.send(message(7, "responsive", {})));
  assert.equal(h.page().unresponsive, false);
  assert.ok(h.send(message(8, "popupBlocked", { id: "4", url: "https://pop.test/4", origin: "https://example.test" })));
  assert.equal(h.page().popups[0].url, "https://pop.test/4");
  assert.ok(h.send(message(9, "crashed", { status: 9, reason: "killed", code: 9 })));
  assert.equal(h.page().crashed.reason, "killed");
});

test("a restore snapshot replaces exactly the producer kinds the native side resends", () => {
  const h = setup(status({ popups: ["1"] }));
  h.send(message(2, "status", { text: "https://example.test/hover" }));
  h.send(message(3, "unresponsive", {}));
  assert.ok(h.send(control(4, "1", { reason: "provisional" })));
  assert.equal(h.send(message(5, "popupBlocked", { id: "2", url: "https://pop.test/2", origin: "" })), false);
  assert.ok(h.send(message(6, "status", { text: "https://example.test/during" })));
  assert.ok(h.send(control(7, "1", { action: "snapshot", reason: "restore", events: status({ text: "https://example.test/during", popups: ["1", "2"] }) })));
  assert.deepEqual(h.page().popups.map((p) => p.id), ["1", "2"]);
  assert.equal(h.page().status, "https://example.test/during");
  assert.equal(h.page().unresponsive, false);
  h.send(message(8, "fullscreen", { fullscreen: true }));
  assert.ok(h.send(control(9, "1", { action: "snapshot", reason: "restore", events: [event("navigation", navigation)] })));
  assert.equal(h.page().fullscreen, true);
  assert.deepEqual(h.page().popups.map((p) => p.id), ["1", "2"]);
});

test("a new document drops every producer's state and stale-epoch events cannot bring it back", () => {
  const h = setup(status({ fullscreen: true, text: "https://example.test/x", hung: true, popups: ["1"] }));
  assert.ok(h.send(control(2, "2")));
  assert.ok(h.send(control(3, "2", { action: "snapshot", events: status() })));
  const page = h.page();
  assert.equal(page.fullscreen, false);
  assert.equal(page.status, "");
  assert.equal(page.unresponsive, false);
  assert.equal(page.crashed, null);
  assert.deepEqual(page.popups, []);
  for (const [name, data] of [["popupBlocked", { id: "1", url: "https://pop.test/1", origin: "" }], ["status", { text: "old" }], ["fullscreen", { fullscreen: true }], ["unresponsive", {}]]) {
    assert.equal(h.send(message(10 + name.length, name, data, { documentEpoch: "1" })), false);
  }
  assert.deepEqual(h.page().popups, []);
  assert.equal(h.page().fullscreen, false);
});

const source = (path) => readFileSync(new URL(`../../../${path}`, import.meta.url), "utf8");

test("native producers, commands and Chromium hooks are wired through the page-state envelope", () => {
  const status = source("engine/ardiacore-linux/src/ardia/linux/ac_linux_page_status.cc");
  const restored = source("apps/linux/src/hostPageLifecycle.ts");
  for (const name of ["fullscreen", "status", "crashed", "unresponsive", "responsive", "popupBlocked"]) {
    assert.match(status, new RegExp(`"${name}"`));
    assert.match(restored, new RegExp(`\\b${name}:`));
  }
  assert.doesNotMatch(status, /->Emit\(\*request, "(fullscreen|status|crashed|unresponsive|responsive)"/);
  const state = source("engine/ardiacore-linux/src/ardia/linux/ac_linux_page_state.cc");
  assert.match(state, /status_->AppendSnapshot\(events\);/);
  const host = source("engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc");
  for (const command of ["resolveUnresponsive", "openBlockedPopup", "downloadFavicon"]) assert.match(host, new RegExp(`command == "${command}"`));
  for (const hook of ["g_ardia_linux_target_url", "g_ardia_linux_renderer_unresponsive", "g_ardia_linux_renderer_responsive"]) {
    assert.match(host, new RegExp(`${hook} = \\[`));
    assert.match(source("engine/patches/chromium-linux-page-status.patch"), new RegExp(`\\+bool \\(\\*${hook}\\)`));
  }
  assert.ok(source("engine/patches/series").split("\n").includes("chromium chromium-linux-page-status.patch linux"));
  assert.match(host, /if \(name == "favicons\.fetch"\) \{/);
  assert.match(host, /capabilities\.Append\("favicons\.fetch"\);/);
  assert.match(host, /geometry_\.FullScreen\(WindowFullScreen\(\)\)/);
  assert.match(host, /if \(WindowFullScreen\(\)\) state\.push_back/);
});
