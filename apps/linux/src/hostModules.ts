import type { ArdiaHost } from "./host";
import { hostModuleMethod, hostSupports } from "./host.ts";

const optionalMethods: Record<string, string[]> = {
  ArdiaCEF: ["claimLaunchTab", "revealTab", "dropTabReveals", "revealOffered", "routeLinkTabs", "setAutoPictureInPicture", "setVideoUpscaling", "setPeekModifier", "setSplitLinkModifier", "setSelectionPopover", "setVideoUpscalingTestConditions", "videoUpscalingState", "inputIdleSeconds", "forwardEngineTopics"],
  ArdiaShell: ["setDragEscapeCapture", "saveKeyResponder", "restoreKeyResponder", "discardKeyResponder", "switcherPointerMoved", "pickFolder", "fetchBangList", "animateWindowClose", "restoreFullScreen", "watchLeftEdge", "dragPreviewBegin", "dragPreviewPage", "dragPreviewUpdate", "dragPreviewCancel", "raiseView", "dragPreviewPlaceholder", "windowFrame", "dragPreviewEnd"],
  ArdiaApp: ["devLeftEdgePointer", "devLeftEdgeStats"],
};

const synchronousDefaults: Record<string, Record<string, unknown>> = {
  ArdiaApp: { apiVersion: 0 },
  ArdiaTranslate: { available: false },
  ArdiaSystem: { backgroundInstance: false },
};

const synchronousMethods: Record<string, Record<string, unknown>> = {
  ArdiaCalendar: { authorizationStatus: "denied" },
  ArdiaGlassEffect: { isLiquidGlass: false },
};

export function resolveHostModule(host: ArdiaHost, name: string, method: string): unknown {
  const constants = host.bootstrap.nativeModules?.[name];
  const sync = synchronousMethods[name];
  if (sync && method in sync) return () => constants?.[method] ?? sync[method];
  if (constants && method in constants) return constants[method];
  const defaults = synchronousDefaults[name];
  if (defaults && method in defaults) return defaults[method];
  if (optionalMethods[name]?.includes(method) && !hostSupports(`${name}.${method}`, host)) return undefined;
  return hostModuleMethod(name, method, host);
}
