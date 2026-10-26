import "./base.css";
import "./styles.css";
import { createRoot } from "react-dom/client";
import { unstable_batchedUpdates } from "react-native";
import { emitCommand, menuShown } from "@ardia/shell";
import { WindowRoot } from "../../browser/src/App";
import { startNativeSync } from "../../browser/src/lib/native";
import { startTabPages } from "../../browser/src/lib/tabPages";
import { setNewTabPrewarm } from "../../browser/src/lib/preload";
import { setStoreBatching, useBrowser, type HydrateData } from "../../browser/src/store/browser";
import { usePages } from "../../browser/src/components/layout/pageState";
import { adoptParkedPins } from "../../browser/src/store/parkedPins";
import { ardiaHost, invokeHost } from "./host";
import { startHostPersistence } from "./hostSession";
import { startHostData } from "./hostData";
import { startHostTabLifecycle } from "./hostTabLifecycle";
import { setHostOverlayCheck } from "./hostViewport";
import { MenuHost } from "./menus";
import { startLinuxShortcuts } from "./shortcutsRuntime";
import { TrafficLights } from "./TrafficLights";
import { createUtilitySessionSnapshot, initialLinuxSession, isLinuxUtility, setSessionFlush } from "./session";
import { startUtility } from "./utility";

Object.assign(globalThis, { ardiaLinux: { store: useBrowser, emitCommand } });

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
    await startUtility(ardiaHost);
  } else {
    useBrowser.setState(adoptParkedPins(useBrowser.getState(), windowId, profileId));
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
    await persistence.flush();
    startTabPages();
    startNativeSync();
  }
  createRoot(document.getElementById("root")!, { onUncaughtError: (error) => console.error("[ardia linux] crashed", error) }).render(
    <div className={`ardia-desktop host ardia-linux${utility ? " utility" : ""}`} data-ardia-linux-window={windowId}>
      <div className="ardia-window">
        <WindowRoot windowId={windowId} />
        <TrafficLights windowId={windowId} />
      </div>
      <MenuHost />
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
