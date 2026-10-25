import "./styles.css";
import { createRoot } from "react-dom/client";
import { unstable_batchedUpdates } from "react-native";
import { emitCommand, menuShown } from "@ardia/shell";
import { WindowRoot } from "../../browser/src/App";
import { startNativeSync } from "../../browser/src/lib/native";
import { startTabPages } from "../../browser/src/lib/tabPages";
import { setStoreBatching, useBrowser } from "../../browser/src/store/browser";
import { Controls } from "./Controls";
import { startEmbed } from "./embed";
import { isFrameable } from "./frameable";
import { pageTitle } from "./pages";
import { installLocalSuggestions } from "./localSuggest";
import { demoSession } from "./seed";
import { warmFrame } from "./shims/web/framePool";
import { MenuHost } from "./shims/web/menus";
import "./shims/web/motion/shortcuts";
import { TrafficLights } from "./shims/web/windowChrome";
import { ardiaHost, invokeHost } from "./host";
import { initialHostSession, startHostPersistence } from "./hostSession";
import type { HydrateData } from "../../browser/src/store/browser";
import { usePages } from "../../browser/src/components/layout/pageState";
import { setHostOverlayCheck } from "./hostViewport";
import { startHostData } from "./hostData";
import { startHostTabLifecycle } from "./hostTabLifecycle";
import { adoptParkedPins } from "../../browser/src/store/parkedPins";

const params = new URLSearchParams(location.search);
const embed = params.has("embed");
const bare = !!ardiaHost || embed || params.has("bare");

Object.assign(globalThis, { ardiaDemo: { store: useBrowser, emitCommand } });

function warmOtherProfiles() {
  const s = useBrowser.getState();
  for (const window of Object.values(s.windows))
    for (const [profileId, tabId] of Object.entries(window.activeTabIds)) {
      const url = s.tabs[tabId]?.url;
      if (profileId !== window.profileId && url && isFrameable(url)) warmFrame(url, pageTitle(url));
    }
}

const idle = (run: () => void) => ("requestIdleCallback" in window ? requestIdleCallback(run, { timeout: 1500 }) : setTimeout(run, 300));
function DemoWindow() {
  return (
    <div className={ardiaHost ? "ardia-desktop host" : embed ? "ardia-desktop embed" : bare ? "ardia-desktop" : "ardia-desktop with-controls"}>
      <div className="ardia-window">
        <WindowRoot windowId={ardiaHost?.bootstrap.windowId} />
        {ardiaHost ? null : <TrafficLights />}
      </div>
      {bare ? null : <Controls />}
      <MenuHost />
    </div>
  );
}

async function start() {
  setStoreBatching(unstable_batchedUpdates);
  if (ardiaHost) {
    const saved = ardiaHost.bootstrap.session ?? await invokeHost<HydrateData | null>("session.read", {});
    useBrowser.getState().hydrate(initialHostSession(ardiaHost, saved));
    useBrowser.setState(adoptParkedPins(useBrowser.getState(), ardiaHost.bootstrap.windowId, ardiaHost.bootstrap.profileId));
    startHostData();
    startHostTabLifecycle(ardiaHost, useBrowser);
    setHostOverlayCheck((key) => {
      const s = useBrowser.getState();
      const owner = key && s.tabs[key];
      const ui = owner ? s.windowUi[owner.windowId] : undefined;
      const pages = usePages.getState();
      const page = key ? pages.pages[key] : undefined;
      return !!(ui?.panel.open || ui?.downloadsOpen || menuShown() || page?.permission || page?.passwordPrompt || page?.autofillPrompt || page?.externalApp || key && pages.popover[key]);
    });
    startHostPersistence(ardiaHost, useBrowser, saved ?? {});
  } else {
    installLocalSuggestions();
    useBrowser.getState().hydrate(demoSession());
    idle(warmOtherProfiles);
  }
  startTabPages();
  startNativeSync();
  createRoot(document.getElementById("root")!, { onUncaughtError: (error) => console.error("[ardia demo] crashed", error) }).render(<DemoWindow />);
  if (embed && !ardiaHost) startEmbed();
}

void start().catch((error) => {
  console.error("[ardia host] startup failed", error);
  const root = document.getElementById("root");
  if (root) {
    root.setAttribute("role", "alert");
    root.textContent = `Ardia could not start: ${error instanceof Error ? error.message : String(error)}`;
  }
});
