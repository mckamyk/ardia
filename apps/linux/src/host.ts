import type { HydrateData } from "../../browser/src/store/browser";

export type HostListener = (module: string, event: string, payload: unknown) => void;
export type ArdiaHost = {
  bootstrap: {
    windowId: string;
    profileId: string;
    startup?: boolean;
    utility?: { pane: string; profileId: string | null; detail: string | null; importSource?: string };
    session?: HydrateData | null;
    documents?: Record<string, string>;
    nativeModules?: Record<string, Record<string, unknown>>;
    capabilities?: string[];
    environment?: Record<string, string>;
  };
  call(name: string, profile: string, args: string | null): Promise<string>;
  invoke(method: string, args: Record<string, unknown>): Promise<unknown>;
  onEvent(listener: HostListener): () => void;
  onBeforeClose?(listener: () => void): () => void;
};

declare global {
  interface Window {
    __ardiaHost?: ArdiaHost;
  }
}

export const ardiaHost = typeof window === "undefined" ? undefined : window.__ardiaHost;

export function hostProfile(profile: string | undefined, host: ArdiaHost = ardiaHost!) {
  return profile || host.bootstrap.profileId;
}

export function invokeHost<T = unknown>(method: string, args: Record<string, unknown>, host: ArdiaHost = ardiaHost!): Promise<T> {
  if (!host) return Promise.reject(new Error(`Ardia host unavailable: ${method}`));
  return Promise.resolve().then(() => host.invoke(method, args)) as Promise<T>;
}

export function hostSupports(method: string, host: ArdiaHost = ardiaHost!) {
  return !!host?.bootstrap.capabilities?.includes(method);
}

export function hostModuleMethod(module: string, method: string, host: ArdiaHost = ardiaHost!) {
  return (...args: unknown[]) => invokeHost(`${module}.${method}`, { args }, host);
}

export function callHost(name: string, profile: string, args: string | null, host: ArdiaHost = ardiaHost!) {
  if (!host) return Promise.reject(new Error(`Ardia host unavailable: ${name}`));
  return Promise.resolve().then(() => host.call(name, hostProfile(profile, host), args));
}

export function reportHostError(error: unknown) {
  console.error("[ardia host]", error);
}
