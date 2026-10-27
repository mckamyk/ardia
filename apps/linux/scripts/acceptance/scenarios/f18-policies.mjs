import { navigateTab, startBrowser } from "../lib/flows.mjs";
import { js, nonce, until } from "../lib/util.mjs";
import { AUTOPLAY_ARGS, mediaPage, startTone, viewCall } from "./f15-media.mjs";

const discarded = (shell, tabId) => shell.eval(`!!ardiaLinux.lifecycle.state.getState().discarded[${js(tabId)}]`);

export default {
  id: "F18",
  issue: 28,
  title: "Tab sleep, freeze protections and battery-aware system state on Linux",
  deadline: 300000,
  manual: [
    { name: "Battery transitions", steps: ["On a laptop with UPower, set Settings › Advanced › Battery Saver to When unplugged.", "Unplug: engine.systemState reports onBattery and one Battery Saver toast appears per focused window; plug in and it turns off."], reason: "needs a real battery and UPower on the system bus" },
    { name: "Power profile", steps: ["Switch power-profiles-daemon to power-saver.", "Within 30 s lowPowerMode turns on and Battery Saver activates."], reason: "needs power-profiles-daemon" },
    { name: "Memory pressure", steps: ["Fill memory until MemAvailable drops under 10% of RAM.", "memoryPressure reports warning (under 5%: critical) and idle background tabs sleep on the next sweep."], reason: "needs real memory pressure" },
  ],
  async run(t) {
    const run = nonce("f18");
    const state = await startBrowser(t, { name: "policies", extraArgs: AUTOPLAY_ARGS });
    const { shell } = state;
    const { url } = t.fixtures;

    await t.check("system state reports memory and an explicit power source", async () => {
      const reply = await shell.call("engine.systemState", {});
      if (!reply.ok) throw new Error(`engine.systemState failed: ${reply.code} ${reply.message}`);
      const value = reply.value;
      if (!["normal", "warning", "critical"].includes(value.memoryPressure) || !(value.physicalMemory > 0)) throw new Error(JSON.stringify(value));
      if (!["upower", "unavailable"].includes(value.power)) throw new Error(`power source ${value.power}`);
      if (value.power === "unavailable" && (value.onBattery !== false || value.batteryLevel !== null)) throw new Error(`unavailable power reported a battery: ${JSON.stringify(value)}`);
      return value;
    }, { critical: true });

    const first = await shell.newTab(url.page(1, run));
    await shell.waitTabUrl(first, (u) => u === url.page(1, run));
    await navigateTab(shell, first, url.page(2, run));
    const visible = await shell.newTab(url.page(3, run));
    await shell.waitTabUrl(visible, (u) => u === url.page(3, run));

    await t.check("the visible tab refuses discard and freeze", async () => {
      const discard = await viewCall(shell, visible, "discard()");
      const frozen = await viewCall(shell, visible, "setFrozen(true)");
      const unload = await viewCall(shell, visible, "discard({ unload: true })");
      if (discard !== "refused" || frozen !== false || unload !== "unsupported") throw new Error(`discard=${discard} frozen=${frozen} unload=${unload}`);
      return { discard, frozen, unload };
    });
    await t.check("an idle background tab reports clean input and sleeps", async () => {
      const unsaved = await viewCall(shell, first, "unsavedInput()");
      if (unsaved !== false) throw new Error(`unsavedInput returned ${JSON.stringify(unsaved)}`);
      const slept = await shell.eval(`ardiaLinux.lifecycle.sleepTab(${js(first)})`);
      if (slept !== true) throw new Error("sleepTab refused an idle background tab");
      await until(() => discarded(shell, first), { timeout: 8000, label: "the shared sleeping state" });
      const again = await viewCall(shell, first, "discard()");
      if (again !== "already") throw new Error(`a second discard returned ${again}`);
      const tab = await shell.tab(first);
      if (!tab || tab.url !== url.page(2, run)) throw new Error(`the Ardia tab changed: ${JSON.stringify(tab?.url)}`);
      return { again };
    }, { critical: true });
    await t.check("activating the sleeping tab wakes it with navigation intact", async () => {
      await shell.state(`s.activate(${js(first)}); return true;`);
      await until(async () => !(await discarded(shell, first)), { timeout: 15000, label: "the tab to wake" });
      await shell.waitTabUrl(first, (u) => u === url.page(2, run), { label: "the woken page" });
      await until(() => shell.state(`return !!s.live[${js(first)}]?.canGoBack;`), { timeout: 10000, label: "back history after waking" });
      return true;
    });
    await t.check("dirty form input protects a background tab", async () => {
      const formUrl = url.media(`${run}-form`);
      const form = await shell.newTab(formUrl);
      await shell.waitTabUrl(form, (u) => u === formUrl);
      const page = await mediaPage(state, formUrl);
      await page.send("Runtime.evaluate", { expression: "document.getElementById('field').focus()", userGesture: true });
      await page.send("Input.insertText", { text: "unsaved draft" });
      await until(async () => (await viewCall(shell, form, "unsavedInput()")) === true, { timeout: 8000, label: "native unsaved input" });
      await shell.state(`s.activate(${js(visible)}); return true;`);
      const slept = await shell.eval(`ardiaLinux.lifecycle.sleepTab(${js(form)})`);
      const discard = await viewCall(shell, form, "discard()");
      const frozen = await viewCall(shell, form, "setFrozen(true)");
      if (slept !== false || discard !== "refused" || frozen !== false) throw new Error(`slept=${slept} discard=${discard} frozen=${frozen}`);
      return { slept, discard, frozen };
    });
    await t.check("an audible background tab is protected and stays playing", async () => {
      const mediaUrl = url.media(`${run}-tone`);
      const tone = await shell.newTab(mediaUrl);
      await shell.waitTabUrl(tone, (u) => u === mediaUrl);
      const page = await mediaPage(state, mediaUrl);
      await startTone(page);
      await until(() => shell.state(`return !!s.live[${js(tone)}]?.playingAudio;`), { timeout: 15000, label: "audible tab" });
      await shell.state(`s.activate(${js(visible)}); return true;`);
      const discard = await viewCall(shell, tone, "discard()");
      if (discard !== "refused") throw new Error(`discard=${discard}`);
      return { discard };
    });
  },
};
