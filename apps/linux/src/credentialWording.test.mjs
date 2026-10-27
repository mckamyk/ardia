import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { CREDENTIAL_WORDING, rewordCredentials } from "../credentialWording.ts";

const shared = (path) => fileURLToPath(new URL(`../../browser/src/${path}`, import.meta.url));
const MACOS = /\bMac\b|macOS|Keychain|Touch ID|Applications folder/;

test("the shared Passwords and Autofill copy is reworded for Linux storage and authentication", () => {
  for (const path of ["components/settings/panes/Passwords.tsx", "components/settings/panes/Autofill.tsx"]) {
    const original = readFileSync(shared(path), "utf8");
    assert.match(original, MACOS, path);
    const reworded = rewordCredentials(shared(path), original);
    assert.ok(reworded, path);
    assert.doesNotMatch(reworded, MACOS, path);
  }
  const rows = readFileSync(shared("components/settings/rows/privacyRows.ts"), "utf8");
  const reworded = rewordCredentials(shared("components/settings/rows/privacyRows.ts"), rows);
  const section = (text, from, to) => text.slice(text.indexOf(from), text.indexOf(to));
  assert.match(section(rows, "passwords: [", "extensions: ["), /Touch ID|keychain/);
  assert.doesNotMatch(section(reworded, "passwords: [", "extensions: ["), /Touch ID|keychain|Keychain|\bMac\b/);
  assert.match(reworded, /system password/);
  assert.match(rewordCredentials(shared("components/settings/panes/Passwords.tsx"), readFileSync(shared("components/settings/panes/Passwords.tsx"), "utf8")), /system password/);
});

test("every replacement still matches the shared source, so a copy change fails the Linux build instead of shipping macOS wording", () => {
  for (const { file, replacements } of CREDENTIAL_WORDING) {
    const source = readFileSync(shared(file), "utf8");
    for (const [from] of replacements) assert.ok(source.includes(from), `${file} no longer contains ${JSON.stringify(from.slice(0, 60))}`);
  }
  assert.throws(() => rewordCredentials(shared("components/settings/panes/Passwords.tsx"), "export const nothing = 1;"), /no longer contains/);
  assert.equal(rewordCredentials(shared("components/settings/panes/General.tsx"), "const x = 'Mac';"), null);
});

test("the Linux Vite config runs the wording transform", () => {
  const config = readFileSync(fileURLToPath(new URL("../vite.config.ts", import.meta.url)), "utf8");
  assert.match(config, /import \{ credentialWording \} from "\.\/credentialWording";/);
  assert.match(config, /plugins: \[[^\]]*credentialWording\(\)/);
});
