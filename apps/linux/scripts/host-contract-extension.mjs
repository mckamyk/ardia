const options = JSON.parse(process.argv[2]);
let captured;
globalThis.window = {
  __ardiaHost: {
    bootstrap: { windowId: "window", profileId: "default" },
    call: async (name, profile, args) => {
      if (name !== "extensions.configure" || profile !== "default") throw new Error("Unexpected domain route");
      captured = args;
      return '{"ok":true}';
    },
    onEvent: () => () => {},
  },
};
const { configureExtension } = await import("../../../packages/ardiacore/src/extensions.ts");
await configureExtension("extension", "default", options);
console.log(captured);
