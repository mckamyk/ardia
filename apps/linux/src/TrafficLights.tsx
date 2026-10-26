import { closeWindow } from "@ardia/shell";
import { TrafficLight, useTrafficLightsCenter } from "./shims/web/windowChrome";
import { hostModuleMethod, reportHostError } from "./host";
import { useBrowser } from "../../browser/src/store/browser";
import { useFullscreenTab } from "../../browser/src/components/layout/pageState";

const controls = [
  { light: "close", method: "closeWindow", label: "Close window" },
  { light: "minimize", method: "minimizeWindow", label: "Minimize window" },
  { light: "zoom", method: "toggleMaximizeWindow", label: "Maximize or restore window" },
] as const;

export function TrafficLights({ windowId }: { windowId: string }) {
  const center = useTrafficLightsCenter();
  const fullscreen = useFullscreenTab(windowId);
  const theme = useBrowser((state) => state.ui.appDark ? "dark" : "light");
  if (!center || fullscreen) return null;
  return (
    <div className="ardia-traffic-lights" aria-label="Window controls" role="group">
      {controls.map(({ light, method, label }, index) => (
        <button key={light} className={`ardia-window-control ${light}`} data-ardia-window-control={light} aria-label={label} title={label} disabled={windowId === "import" && light === "zoom"} style={{ left: center[0] + index * 23 - 7, top: center[1] - 7 }} onClick={() => void (light === "close" ? closeWindow(windowId) : hostModuleMethod("ArdiaShell", method)(windowId)).catch(reportHostError)}>
          <TrafficLight name={light} theme={theme} />
          <span aria-hidden className="ardia-window-control-mark">{light === "close" ? "×" : light === "minimize" ? "−" : "+"}</span>
        </button>
      ))}
    </div>
  );
}
