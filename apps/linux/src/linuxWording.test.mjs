import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { LINUX_WORDING, linuxWording, rewordForLinux } from "../linuxWording.ts";
import { rewordCredentials } from "../credentialWording.ts";
import { unavailablePanes } from "./hostCapabilities.ts";

const shared = (path) => fileURLToPath(new URL(`../../browser/src/${path}`, import.meta.url));
const MACOS = /\bMacs?\b|macOS|iCloud|Keychain|keychain/;
const SURFACES = [
  "state/extensions/actions.ts",
  "components/onboarding/SourceStep.tsx",
  "components/onboarding/LayoutSteps.tsx",
  "components/settings/panes/Appearance.tsx",
  "components/settings/panes/Privacy.tsx",
  "state/telemetry/copy.ts",
  "components/import/steps/ChooseStep.tsx",
  "components/import/steps/SafariStep.tsx",
  "components/import/importActions.ts",
  "components/import/diaActions.ts",
];
const strings = (code) => [...code.matchAll(/"(?:[^"\\\n]|\\.)*"|`(?:[^`\\]|\\.)*`/g)].map((match) => match[0]);
const linuxCopy = (path) => {
  const source = readFileSync(shared(path), "utf8");
  const reworded = rewordForLinux(shared(path), source) ?? source;
  return rewordCredentials(shared(path), reworded) ?? reworded;
};

test("onboarding, Appearance, Privacy, What's Sent and the import flow name no Mac, macOS, iCloud or Keychain on Linux", () => {
  for (const path of SURFACES) {
    const original = readFileSync(shared(path), "utf8");
    assert.ok(strings(original).some((text) => MACOS.test(text)), `${path} has no macOS copy left to reword`);
    const leftover = strings(linuxCopy(path)).filter((text) => MACOS.test(text));
    assert.deepEqual(leftover, [], path);
  }
  assert.match(linuxCopy("components/onboarding/SourceStep.tsx"), /Found on this computer/);
  assert.match(linuxCopy("components/settings/panes/Appearance.tsx"), /title: "Follow system"/);
  assert.match(linuxCopy("components/import/importActions.ts"), /system keyring/);
});

test("the rows that describe those panes are reworded too", () => {
  const general = linuxCopy("components/settings/rows/generalRows.ts");
  const section = (text, from, to) => text.slice(text.indexOf(from), text.indexOf(to, text.indexOf(from)));
  assert.match(section(readFileSync(shared("components/settings/rows/generalRows.ts"), "utf8"), "pageScrollBars: {", "keywords"), /macOS/);
  assert.doesNotMatch(section(general, "pageScrollBars: {", "keywords"), MACOS);
  assert.match(section(general, "pageScrollBars: {", "keywords"), /Follow system/);
  const privacy = linuxCopy("components/settings/rows/privacyRows.ts");
  assert.doesNotMatch(section(privacy, "whatsSent: {", "keywords"), MACOS);
});

test("the Engines pane, whose passkey copy is about macOS, is not offered on Linux without WebKit", () => {
  assert.ok(unavailablePanes({ webkit: false }).has("engines"));
});

test("every replacement still matches the shared source, so a copy change fails the Linux build instead of shipping macOS wording", () => {
  for (const { file, replacements } of LINUX_WORDING) {
    const source = readFileSync(shared(file), "utf8");
    for (const [from] of replacements) {
      assert.ok(source.includes(from), `${file} no longer contains ${JSON.stringify(from.slice(0, 60))}`);
      assert.equal(source.split(from).length, 2, `${file} has more than one ${JSON.stringify(from.slice(0, 60))}`);
    }
  }
  assert.throws(() => rewordForLinux(shared("components/onboarding/SourceStep.tsx"), "export const nothing = 1;"), /no longer contains/);
  assert.equal(rewordForLinux(shared("components/settings/panes/General.tsx"), "const x = 'Mac';"), null);
});

test("the Linux Vite config runs the Linux wording transform", () => {
  const config = readFileSync(fileURLToPath(new URL("../vite.config.ts", import.meta.url)), "utf8");
  assert.match(config, /import \{ linuxWording \} from "\.\/linuxWording";/);
  assert.match(config, /plugins: \[[^\]]*linuxWording\(\)/);
});

test("removing an unpacked extension says its folder stays on this computer", () => {
  const actions = linuxCopy("state/extensions/actions.ts");
  assert.match(actions, /"The extension's folder stays on this computer\."/);
  assert.doesNotMatch(actions, /stays on your Mac/);
});

test("a production build fails when a listed file was loaded but never reworded", () => {
  const context = { error(message) { throw new Error(message); } };
  const run = (command, files, modules = files.map(shared)) => {
    const plugin = linuxWording();
    plugin.configResolved({ command });
    for (const file of files) plugin.transform.call(context, readFileSync(shared(file), "utf8"), shared(file));
    return () => plugin.buildEnd.call({ ...context, getModuleIds: () => [...modules].values() });
  };
  const all = LINUX_WORDING.map((entry) => entry.file);
  assert.doesNotThrow(run("build", all));
  assert.throws(run("build", all.filter((file) => !file.endsWith("SourceStep.tsx")), all.map(shared)), /onboarding\/SourceStep\.tsx/);
  assert.throws(run("build", [], [shared("components/import/ImportWindow.tsx")]), /never reworded/);
  assert.doesNotThrow(run("serve", []));
  assert.doesNotThrow(run("build", [], [shared("components/site/SiteControls.tsx")]));
  const failed = linuxWording();
  failed.configResolved({ command: "build" });
  assert.doesNotThrow(() => failed.buildEnd.call({ ...context, getModuleIds: () => [shared("components/import/ImportWindow.tsx")].values() }, new Error("earlier failure")));
});
