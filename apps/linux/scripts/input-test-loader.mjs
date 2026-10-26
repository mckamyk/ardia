import "../../browser/src/test-loader.mjs";
import { registerHooks } from "node:module";
import { pathToFileURL, fileURLToPath } from "node:url";
import { inputAdapters } from "../inputAdapters.ts";

const adapters = new Set([...inputAdapters.values()].map((path) => pathToFileURL(path).href));
const nativeFixture = new URL("./input-test-native.mjs", import.meta.url).href;
const hostFixture = new URL("./input-test-host.mjs", import.meta.url).href;
const host = new URL("../src/host.ts", import.meta.url).href;
registerHooks({
  resolve(specifier, context, next) {
    if (specifier === "react-native") return { url: nativeFixture, shortCircuit: true };
    const resolved = next(specifier, context);
    if (resolved.url === host) return { url: hostFixture, shortCircuit: true };
    if (!resolved.url.startsWith("file:") || adapters.has(context.parentURL)) return resolved;
    const replacement = inputAdapters.get(fileURLToPath(resolved.url));
    return replacement ? { url: pathToFileURL(replacement).href, shortCircuit: true } : resolved;
  },
});
