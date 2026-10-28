import { startBrowser } from "../lib/flows.mjs";
import { sessionSeed } from "../lib/seed.mjs";
import { until } from "../lib/util.mjs";

export const QUIT_BACKGROUND_ANSWER = '[ardiacore] dialog (background run, not shown): "Are you sure you want to quit Ardia?" (Quit / Cancel): Cancel';
const QUIT_PROMPT = "Are you sure you want to quit Ardia?";
const twoWindows = (settings) => sessionSeed({ windows: [{ id: "main", urls: ["about:blank"] }, { id: "second", urls: ["about:blank"] }], settings });

const browserWindows = async (app) => (await app.shells()).filter((shell) => !shell.utility).length;

async function exitsWithin(app, timeout) {
  await until(() => app.exited, { timeout, label: "Ardia to quit" });
  return app.exitCode;
}

async function restoresBothWindows(t, scratch) {
  const again = await startBrowser(t, { scratch, label: "relaunch" });
  await until(async () => (await browserWindows(again.app)) >= 2, { timeout: 20000, label: "both windows to restore", abort: () => again.app.exitReason() });
  return again;
}

export default {
  id: "Q221",
  issue: 221,
  title: "Warn before quitting asks once, quitting confirms active downloads, and signal quits never ask",
  deadline: 240000,
  manual: [
    {
      name: "The visible quit prompt",
      steps: [
        "In Settings › General turn on Warn before quitting, open two windows and press Ctrl+Shift+Q.",
        `One prompt appears over the focused window only: "${QUIT_PROMPT}", "Your windows and tabs will be restored the next time you open it.", Quit and Cancel, and a "Don't ask again" box. No "Close N tabs?" prompt appears for either window.`,
        "Press Ctrl+Shift+Q again while the prompt is open, in either window: no second prompt appears.",
        "Cancel: both windows and their tabs stay open.",
        "Press Ctrl+Shift+Q, tick Don't ask again, and choose Quit: Ardia quits; after relaunch both windows come back and Settings › General shows the toggle off.",
      ],
      reason: "the prompt is a real modal window; background instances log and auto-answer it",
    },
    {
      name: "Active downloads ask before quitting",
      steps: [
        "Start a large download, then press Ctrl+Shift+Q.",
        "One prompt says \"You have 1 download in progress. If you quit now, this download will be cancelled.\" with Quit and Cancel.",
        "Cancel: Ardia and the download keep running. Quit: Ardia quits with no second download dialog from Chromium.",
        "Start a download again and log out of the desktop session (or send SIGTERM): Ardia quits without asking and restores its windows on the next launch.",
      ],
      reason: "needs a real download and a visible modal window",
    },
  ],
  async run(t) {
    const warned = await startBrowser(t, { name: "warned", session: twoWindows({ warnBeforeQuitting: true }) });
    await t.check("the host offers the quit routes and counts downloads in progress", async () => {
      const capabilities = await warned.shell.eval("__ardiaHost.bootstrap.capabilities");
      for (const route of ["ArdiaShell.quit", "ArdiaShell.downloadsInProgress"]) if (!capabilities?.includes(route)) throw new Error(`${route} is not offered`);
      const count = await warned.shell.invoke("ArdiaShell.downloadsInProgress", { args: [] });
      if (!count?.ok || count.value !== 0) throw new Error(JSON.stringify(count));
      return { downloads: count.value };
    }, { critical: true });
    await t.check("with the warning on, a background instance logs one quit prompt, answers Cancel and keeps running", async () => {
      await until(async () => (await browserWindows(warned.app)) >= 2, { timeout: 20000, label: "both windows", abort: () => warned.app.exitReason() });
      await warned.shell.command("quit");
      await until(async () => (await warned.app.logText()).includes(QUIT_BACKGROUND_ANSWER), { timeout: 15000, label: "the logged quit prompt", abort: () => warned.app.exitReason() });
      await new Promise((resolve) => setTimeout(resolve, 2000));
      const log = await warned.app.logText();
      if (warned.app.exited) throw new Error(warned.app.exitReason());
      if (log.split(QUIT_BACKGROUND_ANSWER).length !== 2) throw new Error("the quit prompt was logged more than once");
      if (/Close \d+ tabs\?/.test(log)) throw new Error("a per-window close prompt was raised");
      if (!(await warned.shell.state("return s.settings.warnBeforeQuitting"))) throw new Error("the warning setting changed");
      return { windows: await browserWindows(warned.app) };
    });
    await t.check("SIGTERM quits without a prompt and saves both windows", async () => {
      const before = (await warned.app.logText()).split("[ardiacore] dialog (background run").length;
      process.kill(warned.app.child.pid, "SIGTERM");
      const code = await exitsWithin(warned.app, 30000);
      const after = (await warned.app.logText()).split("[ardiacore] dialog (background run").length;
      if (after !== before) throw new Error("SIGTERM raised a dialog");
      await t.stop(warned.app);
      const again = await restoresBothWindows(t, warned.scratch);
      await t.stop(again.app);
      return { exitCode: code };
    });
    const plain = await startBrowser(t, { name: "plain", session: twoWindows({ warnBeforeQuitting: false }) });
    await t.check("with the warning off, the quit command quits the whole app once and both windows restore", async () => {
      await until(async () => (await browserWindows(plain.app)) >= 2, { timeout: 20000, label: "both windows", abort: () => plain.app.exitReason() });
      await plain.shell.command("quit");
      const code = await exitsWithin(plain.app, 30000);
      const log = await plain.app.logText();
      if (log.includes("[ardiacore] dialog (background run")) throw new Error("quitting asked a question");
      await t.stop(plain.app);
      const again = await restoresBothWindows(t, plain.scratch);
      await t.stop(again.app);
      return { exitCode: code };
    });
  },
};
