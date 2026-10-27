import { ardiaHost, hostSupports, type ArdiaHost } from "./host.ts";

export type LinuxCapabilities = {
  defaultBrowser: boolean;
  launchAtLogin: boolean;
  thinScrollBars: boolean;
  webkit: boolean;
  performanceRecording: boolean;
  taskManager: boolean;
};

export function linuxCapabilities(host: ArdiaHost | undefined = ardiaHost): LinuxCapabilities {
  const has = (...methods: string[]) => !!host && methods.every((method) => hostSupports(method, host));
  return {
    defaultBrowser: has("ArdiaSystem.isDefaultBrowser", "ArdiaSystem.setAsDefaultBrowser"),
    launchAtLogin: has("ArdiaSystem.launchAtLoginStatus", "ArdiaSystem.setLaunchAtLogin"),
    thinScrollBars: has("ArdiaSystem.setThinScrollBars"),
    webkit: false,
    performanceRecording: has("engine.beginTracing", "engine.endTracing", "engine.isTracing"),
    taskManager: !!host,
  };
}

export function unavailableSettingIds(capabilities: LinuxCapabilities, settingIds: readonly string[]): Set<string> {
  const ids = new Set<string>();
  if (!capabilities.defaultBrowser) ids.add("general.defaultBrowser");
  if (!capabilities.launchAtLogin) ids.add("general.openAtLogin");
  if (!capabilities.thinScrollBars) ids.add("appearance.pageScrollBars");
  if (!capabilities.webkit) for (const id of settingIds) if (id.startsWith("engines.")) ids.add(id);
  return ids;
}

export const unavailablePanes = (capabilities: LinuxCapabilities): Set<string> => new Set(capabilities.webkit ? [] : ["engines"]);

export const unavailableError = (feature: string) => Object.assign(new Error(`${feature} is unavailable on Linux`), { code: "ERR_UNAVAILABLE" });
