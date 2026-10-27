import { MODIFIER } from "../lib/ardia.mjs";
import { findCompositor, startNestedCompositor } from "../lib/compositor.mjs";
import { startBrowser } from "../lib/flows.mjs";
import { until } from "../lib/util.mjs";

const MANUAL_FOCUS = [
  { name: "Ctrl+N then immediate typing lands in the new window", steps: ["On Hyprland (Wayland) and on an X11 session, focus an Ardia window.", "Press Ctrl+N and type 'abc' at once.", "The new window is in front and its command bar holds 'abc'; the old window received nothing."] },
  { name: "Background and restore-behind windows never activate", steps: ["Quit Ardia with two windows open and relaunch: the restored back window stays behind.", "Open a link from another app with Ardia in the background: Ardia doesn't take focus unless the link requires a new foreground window.", "Run a hidden test instance (ARDIA_BACKGROUND=1): no window of it ever activates."] },
  { name: "Settings and Import come to the front", steps: ["With a browser window focused, open Settings (Ctrl+,) and File › Import.", "Each opens in front and has keyboard focus; opening Settings again focuses the existing window."] },
];

const hasFocus = (shell) => shell.eval("document.hasFocus()");

export default {
  id: "F22",
  issue: 32,
  title: "New windows take requested foreground focus and background windows never do",
  deadline: 240000,
  async run(t) {
    const found = await findCompositor();
    if (!found) {
      for (const item of MANUAL_FOCUS) t.manual(item.name, item.steps, "no nested Wayland compositor (sway, weston or cage) is installed");
      return;
    }
    const nestDir = await t.scratch("compositor");
    const nested = await startNestedCompositor(found, nestDir);
    if (nested.unsupported) {
      for (const item of MANUAL_FOCUS) t.manual(item.name, item.steps, nested.unsupported);
      return;
    }
    let session;
    try {
      session = await startBrowser(t, { name: "focus", background: false, ozone: "wayland", env: nested.env, extraArgs: ["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
      const { app, shell } = session;
      await t.check(`${nested.name}: the first window is active`, () => until(() => hasFocus(shell), { timeout: 10000, label: "first window focus" }));
      let created;
      await t.check(`${nested.name}: Ctrl+N focuses the new window`, async () => {
        const before = await shell.windowIds();
        await shell.key("n", { code: "KeyN", keyCode: 78, modifiers: MODIFIER.ctrl });
        const id = await until(async () => (await shell.windowIds()).find((w) => !before.includes(w)) ?? false, { timeout: 15000, label: "the Ctrl+N window" });
        created = await app.shell(id, { timeout: 20000 });
        await until(() => hasFocus(created), { timeout: 10000, label: "focus in the new window" });
        if (await hasFocus(shell)) throw new Error("the old window still reports focus");
        return { id };
      });
      await t.check(`${nested.name}: a background window does not take focus`, async () => {
        const before = await shell.windowIds();
        const id = await created.state("return s.createWindow({ background: true });");
        await until(async () => (await shell.windowIds()).includes(id), { timeout: 15000, label: "background window" });
        const background = await app.shell(id, { timeout: 20000 });
        await new Promise((resolve) => setTimeout(resolve, 1500));
        if (await hasFocus(background)) throw new Error("the background window took focus");
        if (!(await hasFocus(created))) throw new Error("the focused window lost focus to a background window");
        return { id, before };
      });
      await t.check(`${nested.name}: Settings comes to the front`, async () => {
        await created.command("openSettings");
        const settings = await app.shell("settings", { timeout: 20000 });
        await until(() => hasFocus(settings), { timeout: 10000, label: "Settings focus" });
        return true;
      });
      t.manual(MANUAL_FOCUS[0].name, MANUAL_FOCUS[0].steps, "typing through the compositor needs a virtual keyboard; the nested check above covers activation only");
      t.manual(MANUAL_FOCUS[1].name, MANUAL_FOCUS[1].steps.slice(0, 2), "restore-behind and links from other apps need a real desktop session");
    } finally {
      if (session) await t.stop(session.app);
      await nested.stop();
    }
  },
};
