import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { resolveHostModule } from "./hostModules.ts";

const host = readFileSync(new URL("../../../engine/ardiacore-linux/src/ardia/linux/ac_linux_host.cc", import.meta.url), "utf8");
const modules = readFileSync(new URL("./hostModules.ts", import.meta.url), "utf8");
const bootstrap = host.slice(host.indexOf("base::DictValue ShellHostView::Bootstrap() const {"), host.indexOf("void ShellHostView::LayoutOverlay()"));
const advertised = new Set([...bootstrap.matchAll(/\.Append\("([A-Za-z]+\.[A-Za-z]+)"\)/g)].map((match) => match[1]));
const optional = [...modules.slice(modules.indexOf("ArdiaShell: ["), modules.indexOf("]", modules.indexOf("ArdiaShell: ["))).matchAll(/"([A-Za-z]+)"/g)].map((match) => match[1]);
const implemented = (method) => host.includes(`method == "ArdiaShell.${method}"`);

const fakeHost = (capabilities) => ({
  bootstrap: { windowId: "main", profileId: "default", capabilities },
  invoke: async () => null,
  call: async () => "null",
  onEvent: () => () => {},
});

test("every optional shell method the native host implements is advertised, so the real shell module can call it", () => {
  const missing = optional.filter((method) => implemented(method) && method !== "windowFrame" && !advertised.has(`ArdiaShell.${method}`));
  assert.deepEqual(missing, []);
  for (const method of ["restoreFullScreen", "typeaheadReady"]) assert.ok(advertised.has(`ArdiaShell.${method}`), method);
});

test("an advertised fullscreen restore resolves to host calls instead of undefined", () => {
  const capabilities = [...advertised];
  for (const method of ["restoreFullScreen"]) {
    assert.equal(typeof resolveHostModule(fakeHost(capabilities), "ArdiaShell", method), "function", method);
    assert.equal(resolveHostModule(fakeHost([]), "ArdiaShell", method), undefined, method);
  }
});
