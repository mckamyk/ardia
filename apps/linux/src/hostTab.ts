import type { ArdiaHost } from "./host";
import { enqueueTab } from "./hostTabQueue.ts";
import { queueTabPresentation } from "./hostTabPresentations.ts";
import { hostProfile, invokeHost } from "./host.ts";

export type HostTabEvent = { type: string; [key: string]: unknown };
export type HostTabIdentity = { browserId: number; chromeTabId: number; state?: unknown };
export type HostTabOptions = { key: string; profile?: string; url?: string; adoptId?: string; temporary?: boolean };
export type TabViewport = { rect: { x: number; y: number; width: number; height: number }; visible: boolean };

const claims = new WeakMap<ArdiaHost, Map<string, HostTab>>();

export class HostTab {
  host: ArdiaHost;
  key: string;
  claim = crypto.randomUUID();
  ready: Promise<HostTabIdentity>;
  identity: HostTabIdentity | undefined;
  disposed = false;
  unsubscribe: () => void;
  lastViewport = "";
  releaseScheduled = false;

  constructor(host: ArdiaHost, options: HostTabOptions, onEvent: (event: HostTabEvent) => void) {
    this.host = host;
    this.key = options.key;
    const owners = claims.get(host) ?? new Map<string, HostTab>();
    claims.set(host, owners);
    const early: { id: string; event: HostTabEvent }[] = [];
    const deliver = (event: HostTabEvent) => {
      try { onEvent(event); } catch (error) { console.error("[ardia host] tab listener failed", error); }
    };
    this.unsubscribe = host.onEvent((module, id, payload) => {
      if (this.disposed || (this.identity && owners.get(this.key) !== this) || module !== "ArdiaWebView" || !payload || typeof payload !== "object" || !("type" in payload)) return;
      const event = payload as HostTabEvent;
      if (!this.identity) {
        if (early.length === 128) early.shift();
        early.push({ id, event });
      } else if (id === String(this.identity.browserId)) deliver(event);
    });
    this.ready = enqueueTab(host, this.key, async () => {
      const previousClaim = owners.get(this.key)?.claim;
      const identity = await invokeHost<HostTabIdentity>("tab.create", {
        ...options, claim: this.claim, ...(previousClaim ? { previousClaim } : {}), profile: hostProfile(options.profile, host), url: options.url ?? "about:blank",
      }, host);
      if (!Number.isInteger(identity?.browserId) || identity.browserId <= 0 || !Number.isInteger(identity.chromeTabId) || identity.chromeTabId <= 0) throw new Error("Invalid Ardia tab identity");
      this.identity = identity;
      owners.set(this.key, this);
      if (this.disposed) this.detach();
      else if (owners.get(this.key) === this) {
        deliver({ type: "ready", ...identity });
        if (identity.state) deliver({ type: "navigation", state: identity.state });
        for (const queued of early) if (queued.id === String(identity.browserId) && queued.event.type !== "ready") deliver(queued.event);
      }
      early.length = 0;
      return identity;
    }).catch((error) => {
      early.length = 0;
      this.unsubscribe();
      if (owners.get(this.key) === this) owners.delete(this.key);
      throw error;
    });
  }

  async command<T = unknown>(command: string, args: unknown[] = []): Promise<T> {
    const { browserId } = await this.ready;
    return enqueueTab(this.host, this.key, async () => {
      if (this.disposed || claims.get(this.host)?.get(this.key) !== this) throw new Error(`Ardia tab released: ${this.key}`);
      return invokeHost<T>("tab.command", { id: browserId, claim: this.claim, command, args }, this.host);
    });
  }

  presentation(visible: boolean): Promise<void> {
    return queueTabPresentation(this, visible, () => claims.get(this.host)?.get(this.key) === this);
  }

  viewport(viewport: TabViewport) {
    return enqueueTab(this.host, this.key, async () => {
      if (this.disposed || !this.identity || claims.get(this.host)?.get(this.key) !== this) return;
      const json = JSON.stringify(viewport);
      if (json === this.lastViewport) return;
      this.lastViewport = json;
      return invokeHost<void>("tab.viewport", { id: this.identity.browserId, claim: this.claim, ...viewport }, this.host).catch((error) => {
        if (this.lastViewport === json) this.lastViewport = "";
        throw error;
      });
    });
  }

  detach() {
    if (this.releaseScheduled) return;
    this.releaseScheduled = true;
    void enqueueTab(this.host, this.key, async () => {
      const owners = claims.get(this.host);
      if (!this.identity || owners?.get(this.key) !== this) return;
      await invokeHost("tab.release", { id: this.identity.browserId, claim: this.claim }, this.host);
      if (owners.get(this.key) === this) owners.delete(this.key);
    }).catch((error) => {
      this.releaseScheduled = false;
      console.error("[ardia host] release failed", error);
    });
  }

  dispose() {
    this.disposed = true;
    this.unsubscribe();
    this.detach();
  }
}
