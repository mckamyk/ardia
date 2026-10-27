import type { ArdiaHost } from "./host.ts";
import { linuxCapabilities, unavailableError } from "./hostCapabilities.ts";

export function linuxSystemMethod(host: ArdiaHost, method: string): ((...args: unknown[]) => Promise<unknown>) | undefined {
  const capabilities = linuxCapabilities(host);
  switch (method) {
    case "isDefaultBrowser":
    case "setAsDefaultBrowser":
      return capabilities.defaultBrowser ? undefined : () => Promise.reject(unavailableError("Setting the default browser"));
    case "launchAtLoginStatus":
      return capabilities.launchAtLogin ? undefined : () => Promise.resolve("notFound");
    case "setLaunchAtLogin":
      return capabilities.launchAtLogin ? undefined : () => Promise.reject(unavailableError("Opening at login"));
    case "setThinScrollBars":
      return capabilities.thinScrollBars ? undefined : () => Promise.resolve();
  }
  return undefined;
}
