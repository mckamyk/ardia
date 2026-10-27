import "./base.css";
import "./styles.css";
import { useEffect } from "react";
import { createRoot } from "react-dom/client";
import { unstable_batchedUpdates } from "react-native";
import { emitCommand, menuShown, setWindowCloseDecider } from "@ardia/shell";
import { WindowRoot } from "../../browser/src/App";
import { startNativeSync } from "../../browser/src/state/app/nativeSync";
import { startTabPages } from "../../browser/src/state/tabs/tabPages";
import { setNewTabPrewarm } from "../../browser/src/platform/preload";
import { setStoreBatching, useBrowser, type HydrateData } from "../../browser/src/state/browser/store";
import { usePages } from "../../browser/src/state/tabs/pageState";
import { adoptParkedPins } from "../../browser/src/state/pins/parkedPins";
import { decideLastTabClose, decideWindowClose } from "../../browser/src/state/windows/windowClose";
import { firstWindowCommitted } from "../../browser/src/lib/afterFirstWindow";
import { recordFreshProfile, startAppCommands, startLinuxAppServices, UTILITY_APP_COMMANDS } from "./appServices";
import { linuxCapabilities } from "./hostCapabilities";
import { ardiaHost, invokeHost } from "./host";
import { startHostPersistence } from "./hostSession";
import { startHostData } from "./hostData";
import { startHostDownloads } from "./hostDownloads";
import { startHostTabLifecycle } from "./hostTabLifecycle";
import { setHostPageHooks } from "./hostPageHooks";
import { applyHostPageTheme, resetHostPage } from "./hostPageLifecycle";
import { setHostOverlayCheck } from "./hostViewport";
import { MenuHost } from "./menus";
import { startLinuxShortcuts } from "./shortcutsRuntime";
import { TrafficLights } from "./TrafficLights";
import { createUtilitySessionSnapshot, initialLinuxSession, isLinuxUtility, setSessionDeparture, setSessionFlush } from "./session";
import { startUtility } from "./utility";
import { installProfileWindows } from "./hostWindowProfiles";
import { focus } from "../../browser/src/state/browser/actions";

Object.assign(globalThis, { ardiaLinux: { store: useBrowser, emitCommand } });
setHostPageHooks({ reset: resetHostPage, applyTheme: applyHostPageTheme });
setWindowCloseDecider((windowId, reason, ask) => reason === "lastTab" ? decideLastTabClose(windowId, ask) : decideWindowClose(windowId, ask));

function FirstWindowCommitted() {
  useEffect(() => firstWindowCommitted(), []);
  return null;
}

async function start() {
  if (!ardiaHost) throw new Error("The Linux desktop UI requires an Ardia native host");
  setNewTabPrewarm(false);
  const { windowId, profileId } = ardiaHost.bootstrap;
  const utility = isLinuxUtility(windowId);
  const saved = ardiaHost.bootstrap.session ?? await invokeHost<HydrateData | null>("session.read", {});
  setStoreBatching(unstable_batchedUpdates);
  useBrowser.getState().hydrate(initialLinuxSession(ardiaHost, saved));
  await startLinuxShortcuts(ardiaHost);
  if (utility) {
    const persistence = startHostPersistence(ardiaHost, useBrowser, saved ?? {}, createUtilitySessionSnapshot(useBrowser.getState(), saved ?? {}));
    setSessionFlush(persistence.flush);
    startHostData();
    startAppCommands(windowId, linuxCapabilities(ardiaHost), undefined, UTILITY_APP_COMMANDS);
    await startUtility(ardiaHost);
  } else {
    if (!ardiaHost.bootstrap.privateWindow) useBrowser.setState(adoptParkedPins(useBrowser.getState(), windowId, profileId));
    installProfileWindows(useBrowser, focus);
    startHostData();
    startHostTabLifecycle(ardiaHost, useBrowser);
    setHostOverlayCheck((key) => {
      const state = useBrowser.getState();
      const owner = key && state.tabs[key];
      const ui = owner ? state.windowUi[owner.windowId] : undefined;
      const pages = usePages.getState();
      const page = key ? pages.pages[key] : undefined;
      return !!(ui?.panel.open || ui?.downloadsOpen || menuShown() || page?.permission || page?.passwordPrompt || page?.autofillPrompt || page?.externalApp || key && pages.popover[key]);
    });
    const persistence = startHostPersistence(ardiaHost, useBrowser, saved ?? {});
    setSessionFlush(persistence.flush);
    setSessionDeparture(persistence);
    if (!saved) await recordFreshProfile();
    await persistence.flush();
    startTabPages();
    startNativeSync();
    startHostDownloads(ardiaHost, useBrowser);
    startLinuxAppServices({
      host: ardiaHost,
      windowId,
      savedSession: !!saved,
      capabilities: linuxCapabilities(ardiaHost),
      locks: navigator.locks,
      marks: sessionStorage,
    });
  }
  createRoot(document.getElementById("root")!, { onUncaughtError: (error) => console.error("[ardia linux] crashed", error) }).render(
    <div className={`ardia-desktop host ardia-linux${utility ? " utility" : ""}`} data-ardia-linux-window={windowId}>
      <div className="ardia-window">
        <WindowRoot windowId={windowId} />
        <TrafficLights windowId={windowId} />
      </div>
      <MenuHost />
      <FirstWindowCommitted />
    </div>,
  );
}

void start().catch((error) => {
  console.error("[ardia linux] startup failed", error);
  const root = document.getElementById("root");
  if (root) {
    root.setAttribute("role", "alert");
    root.textContent = `Ardia could not start: ${error instanceof Error ? error.message : String(error)}`;
  }
});
