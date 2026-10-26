const stub = new URL("../../browser/src/test-native-stub.mjs", import.meta.url).href;
const native = new Set(["@ardia/shell", "@ardia/ardiacore", "expo-modules-core"]);

export async function resolve(specifier, context, next) {
  if (native.has(specifier)) return { url: stub, shortCircuit: true };
  try {
    return await next(specifier, context);
  } catch (error) {
    if (specifier.startsWith(".")) {
      for (const ext of [".ts", ".tsx"]) {
        try {
          return await next(specifier + ext, context);
        } catch {}
      }
    }
    throw error;
  }
}
