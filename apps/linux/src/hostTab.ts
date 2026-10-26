import type { ArdiaHost } from "./host";
import { hostProfile, invokeHost } from "./host.ts";

export type HostTabEvent = { type: string; [key: string]: unknown };
export type HostTabIdentity = { browserId: number; chromeTabId: number; state?: unknown };
export type HostTabOptions = { key: string; profile?: string; url?: string; adoptId?: string };
export type TabViewport = { rect: { x: number; y: number; width: number; height: number }; visible: boolean };

const claims = new WeakMap<ArdiaHost, Map<string, HostTab>>();

export class HostTab {
  host: ArdiaHost;
  key: string;
  ready: Promise<HostTabIdentity>;
  identity: HostTabIdentity | undefined;
  disposed = false;
  unsubscribe: () => void;
  lastViewport = "";

  constructor(host: ArdiaHost, options: HostTabOptions, onEvent: (event: HostTabEvent) => void) {
    this.host = host;
    this.key = options.key;
    const owners = claims.get(host) ?? new Map<string, HostTab>();
    claims.set(host, owners);
    owners.set(this.key, this);
    const early: { id: string; event: HostTabEvent }[] = [];
    this.unsubscribe = host.onEvent((module, id, payload) => {
      if (this.disposed || owners.get(this.key) !== this || module !== "ArdiaWebView" || !payload || typeof payload !== "object" || !("type" in payload)) return;
      const event = payload as HostTabEvent;
      if (!this.identity) {
        if (early.length === 128) early.shift();
        early.push({ id, event });
      } else if (id === String(this.identity.browserId)) onEvent(event);
    });
    this.ready = invokeHost<HostTabIdentity>("tab.create", {
      ...options, profile: hostProfile(options.profile, host), url: options.url ?? "about:blank",
    }, host).then((identity) => {
      if (!Number.isInteger(identity?.browserId) || identity.browserId <= 0 || !Number.isInteger(identity.chromeTabId) || identity.chromeTabId <= 0) throw new Error("Invalid Ardia tab identity");
      this.identity = identity;
      if (this.disposed) this.detach();
      else if (owners.get(this.key) === this) {
        onEvent({ type: "ready", ...identity });
        if (identity.state) onEvent({ type: "navigation", state: identity.state });
        for (const queued of early) if (queued.id === String(identity.browserId) && queued.event.type !== "ready") onEvent(queued.event);
      }
      early.length = 0;
      return identity;
    }).catch((error) => {
      this.unsubscribe();
      if (owners.get(this.key) === this) owners.delete(this.key);
      throw error;
    });
  }

  async command<T = unknown>(command: string, args: unknown[] = []): Promise<T> {
    const { browserId } = await this.ready;
    if (this.disposed || claims.get(this.host)?.get(this.key) !== this) throw new Error(`Ardia tab released: ${this.key}`);
    return invokeHost<T>("tab.command", { id: browserId, command, args }, this.host);
  }

  viewport(viewport: TabViewport) {
    if (this.disposed || !this.identity || claims.get(this.host)?.get(this.key) !== this) return Promise.resolve();
    const json = JSON.stringify(viewport);
    if (json === this.lastViewport) return Promise.resolve();
    this.lastViewport = json;
    return invokeHost<void>("tab.viewport", { id: this.identity.browserId, ...viewport }, this.host).catch((error) => {
      if (this.lastViewport === json) this.lastViewport = "";
      throw error;
    });
  }

  detach() {
    const owners = claims.get(this.host);
    if (!this.identity || owners?.get(this.key) !== this) return;
    owners.delete(this.key);
    void invokeHost("tab.release", { id: this.identity.browserId }, this.host).catch((error) => console.error("[ardia host] release failed", error));
  }

  dispose() {
    this.disposed = true;
    this.unsubscribe();
    this.detach();
  }
}
