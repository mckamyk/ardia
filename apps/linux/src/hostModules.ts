import { hostLaunchConfig } from "./hostLaunchConfig.ts";
import type { ArdiaHost } from "./host";
import { callHost, hostModuleMethod, hostSupports } from "./host.ts";
import { credentialModuleMethod } from "./hostCredentials.ts";
import { linuxSystemMethod } from "./hostFiles.ts";
import { linuxModuleMethod } from "./hostSystem.ts";

const optionalMethods: Record<string, string[]> = {
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
  if (name === "ArdiaApp" && method === "launchConfig") return () => hostLaunchConfig(host);
  if (name === "ArdiaSystem") {
    const local = linuxSystemMethod(host, method);
    if (local !== undefined) return local;
  }
  const platform = linuxModuleMethod(host, name, method);
  if (platform) return platform;
  const credential = credentialModuleMethod(host, name, method);
  if (credential !== undefined) return credential;
  if (name === "ArdiaDocuments" && method === "call") {
    return (verb: string, profile: string, args: string) => {
      if (!["watch", "commit", "flush", "unwatch"].includes(verb)) return Promise.reject(Object.assign(new Error("Unknown document operation"), { code: "ERR_UNKNOWN_CALL" }));
      if (typeof args !== "string") return Promise.reject(Object.assign(new Error("Document arguments require JSON text"), { code: "ERR_DOCUMENT_REQUEST" }));
      return callHost(`documents.${verb}`, profile, args, host);
    };
  }
  if (method === "call" && /^Ardia[A-Z]/.test(name)) {
    const domain = name.slice(5, 6).toLowerCase() + name.slice(6);
    return (verb: string, profile: string, args: string) => callHost(`${domain}.${verb}`, profile, args, host);
  }
  const constants = host.bootstrap.nativeModules?.[name];
  const sync = synchronousMethods[name];
  if (sync && method in sync) return () => constants?.[method] ?? sync[method];
  if (constants && method in constants) return constants[method];
  const defaults = synchronousDefaults[name];
  if (defaults && method in defaults) return defaults[method];
  if (optionalMethods[name]?.includes(method) && !hostSupports(`${name}.${method}`, host)) return undefined;
  return hostModuleMethod(name, method, host);
}
