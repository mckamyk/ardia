import { evaluate } from "../lib/cdp.mjs";
import { MEDIA_TITLE } from "../lib/fixtures.mjs";
import { navigateTab, startBrowser } from "../lib/flows.mjs";
import { js, nonce, until } from "../lib/util.mjs";

export const AUTOPLAY_ARGS = ["--autoplay-policy=no-user-gesture-required"];
export const PIP_PROBE = 'post("result", JSON.stringify(!!document.pictureInPictureElement || !!window.documentPictureInPicture?.window))';

export function viewCall(shell, tabId, call) {
  return shell.eval(`(async () => { const view = ardiaLinux.webviews.get(${js(tabId)}); if (!view) throw new Error("tab ${tabId} has no mounted page"); return await view.${call}; })()`);
}

export async function viewCallWithoutGesture(shell, tabId, call) {
  const expression = `(async () => { const view = ardiaLinux.webviews.get(${js(tabId)}); if (!view) throw new Error("no mounted page"); return await view.${call}; })()`;
  const result = await shell.session.send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true, userGesture: false }, { timeout: 10000 });
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description ?? result.exceptionDetails.text);
  return result.result?.value;
}

export async function mediaPage(state, url) {
  const target = await state.app.pageLoaded(url);
  return state.app.connect(target);
}

export async function startTone(page) {
  const started = await evaluate(page, "startTone()");
  if (started !== true) throw new Error(`the fixture tone did not start: ${started}`);
  return until(() => evaluate(page, "!document.getElementById('tone').paused && document.getElementById('tone').currentTime > 0"), { timeout: 10000, label: "the tone to play" });
}

const session = (shell, tabId) => shell.eval(`ardiaLinux.media.getState().sessions[${js(tabId)}] ?? null`);
const audible = (shell, tabId) => shell.state(`return !!s.live[${js(tabId)}]?.playingAudio;`);

export default {
  id: "F15",
  issue: 25,
  title: "Media badges, now-playing transport and manual Picture-in-Picture through the native host",
  deadline: 300000,
  manual: [
    { name: "Manual PiP of a real video", steps: ["Play a video page (for example a local WebM).", "Use the tab's Picture-in-Picture control in the sidebar or mini player.", "A PiP window opens, the control shows the open state, and pressing it again closes the window and returns to the tab."], reason: "headless Chromium has no compositor surface for a PiP window" },
    { name: "Automatic PiP still separate", steps: ["With Settings › Tabs › Automatic Picture in Picture on, switch away from a playing video tab.", "The PiP window opens on Hyprland at the configured placement and closes when you return; with the setting off nothing opens."], reason: "needs a compositor" },
    { name: "Hear mute", steps: ["Mute a playing tab from its audio badge.", "The sound stops, the badge shows muted, and unmuting restores sound."], reason: "needs an audio device" },
    { name: "Portal capture indicators", steps: ["Share a screen through the xdg-desktop-portal picker on a test page.", "The tab shows the screen-sharing indicator while capturing and it clears when sharing stops."], reason: "needs a desktop portal session" },
  ],
  async run(t) {
    const run = nonce("f15");
    const state = await startBrowser(t, { name: "media", extraArgs: AUTOPLAY_ARGS });
    const { shell } = state;
    const url = t.fixtures.url.media(run);
    const tabId = await shell.newTab(url);
    await shell.waitTabUrl(tabId, (u) => u === url);
    const page = await mediaPage(state, url);

    await t.check("playing audio reaches the tab's audio badge state", async () => {
      await startTone(page);
      await until(() => audible(shell, tabId), { timeout: 15000, label: "s.live[tab].playingAudio" });
      return { audible: true };
    }, { critical: true });
    await t.check("now-playing metadata, actions and position reach the shared player", async () => {
      const current = await until(async () => {
        const s = await session(shell, tabId);
        return s?.title === MEDIA_TITLE && s.playbackState === "playing" && s.actions.includes("nexttrack") ? s : false;
      }, { timeout: 15000, label: "the media session in useMedia" });
      if (current.artist !== "Ardia" || current.album !== "Fixture") throw new Error(`metadata ${JSON.stringify(current)}`);
      if (!current.controllable) throw new Error("the session is not controllable");
      return { title: current.title, actions: current.actions, artwork: current.artwork, position: current.position };
    });
    await t.check("pause and play from Ardia reach the page's media session", async () => {
      await viewCall(shell, tabId, "mediaCommand('pause')");
      await until(() => evaluate(page, "document.getElementById('tone').paused && mediaLog.includes('pause')"), { timeout: 8000, label: "the page to pause" });
      await until(async () => (await session(shell, tabId))?.playbackState === "paused", { timeout: 8000, label: "the shared session to show paused" });
      await viewCall(shell, tabId, "mediaCommand('toggle')");
      await until(() => evaluate(page, "!document.getElementById('tone').paused && mediaLog.includes('play')"), { timeout: 8000, label: "the page to resume" });
      await until(async () => (await session(shell, tabId))?.playbackState === "playing", { timeout: 8000, label: "the shared session to show playing" });
      return { log: await evaluate(page, "mediaLog") };
    });
    await t.check("next track and seek reach the page and invalid commands are refused", async () => {
      await viewCall(shell, tabId, "mediaCommand('next')");
      await viewCall(shell, tabId, "mediaCommand('seekTo', 0.25)");
      await until(() => evaluate(page, "mediaLog.includes('nexttrack') && mediaLog.includes('seekto')"), { timeout: 8000, label: "nexttrack and seekto handlers" });
      const refused = await shell.eval(`ardiaLinux.webviews.get(${js(tabId)}).mediaCommand('rewind').then(() => false, () => true)`);
      if (!refused) throw new Error("an unknown media command was accepted");
      return { log: await evaluate(page, "mediaLog") };
    });
    await t.check("muting is accepted and keeps the session", async () => {
      await viewCall(shell, tabId, "setMuted(true)");
      const s = await session(shell, tabId);
      await viewCall(shell, tabId, "setMuted(false)");
      return { session: s?.title ?? null };
    });
    await t.check("the PiP probe answers natively and an audio-only page opens no window", async () => {
      const open = await viewCall(shell, tabId, `evaluate(${js(PIP_PROBE)})`);
      if (open !== false) throw new Error(`the PiP probe returned ${JSON.stringify(open)}`);
      const requested = await viewCall(shell, tabId, "requestPictureInPicture({ clickAt: 0, control: 0 })");
      if (requested !== false) throw new Error(`an audio-only page reported PiP ${JSON.stringify(requested)}`);
      await viewCall(shell, tabId, "exitPictureInPicture()");
      return { open, requested };
    });
    await t.check("a PiP request without user activation is refused", async () => {
      await new Promise((resolve) => setTimeout(resolve, 5500));
      const requested = await viewCallWithoutGesture(shell, tabId, "requestPictureInPicture({ clickAt: 0, control: 0 })");
      const taken = await viewCallWithoutGesture(shell, tabId, "takeControlPress({ clickAt: 0, control: 0 })");
      if (requested !== false || taken !== false) throw new Error(`requested=${requested} taken=${taken}`);
      return { requested, taken };
    });
    await t.check("a playing background tab is protected from sleep, discard and freeze", async () => {
      const other = await shell.newTab(t.fixtures.url.page(1, run));
      await shell.waitTabUrl(other, (u) => u.startsWith(t.fixtures.url.page(1, "")));
      if (!(await shell.eval(`ardiaLinux.webviews.has(${js(tabId)})`))) throw new Error("the media tab is no longer mounted");
      const slept = await shell.eval(`ardiaLinux.lifecycle.sleepTab(${js(tabId)})`);
      const discard = await viewCall(shell, tabId, "discard()");
      const frozen = await viewCall(shell, tabId, "setFrozen(true)");
      if (slept !== false || discard !== "refused" || frozen !== false) throw new Error(`slept=${slept} discard=${discard} frozen=${frozen}`);
      if (!(await evaluate(page, "!document.getElementById('tone').paused"))) throw new Error("playback stopped");
      return { slept, discard, frozen };
    });
    await t.check("navigating away clears the player", async () => {
      await navigateTab(shell, tabId, t.fixtures.url.page(2, run));
      await until(async () => (await session(shell, tabId)) === null, { timeout: 10000, label: "the session to clear" });
      await until(async () => !(await audible(shell, tabId)), { timeout: 10000, label: "the audio badge to clear" });
      return true;
    });
  },
};
