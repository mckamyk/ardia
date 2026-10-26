import { toAppUrl } from "@ardia/core";
import { deliverPageEvent, type PageEvent } from "../../../packages/ardiacore/src/pageEvents";
import type { NavigationState, WebViewProps } from "../../../packages/ardiacore/src/pageTypes";
import type { PageResetReason } from "./hostPageLifecycle";

export type PageLeaseMetadata = {
  claim: string;
  rendererGeneration: string;
  bindingGeneration: string;
  documentEpoch: string;
};
export type PageDelivery = PageLeaseMetadata & { type: "pageEvent"; sequence: number; event: PageEvent };
export type PageLifecycle = PageLeaseMetadata & { type: "pageLifecycle"; sequence: number } & (
  { action: "reset"; reason: "document" | "provisional" } |
  { action: "snapshot"; events: PageEvent[] }
);
export type PageCreationState = { lease: PageLeaseMetadata; snapshot: { watermark: number; events: PageEvent[] } };

const record = (value: unknown): value is Record<string, unknown> => !!value && typeof value === "object" && !Array.isArray(value);
const generation = (value: unknown): value is string => typeof value === "string" && /^[1-9][0-9]{0,19}$/.test(value) && BigInt(value) <= 18446744073709551615n;
const sequence = (value: unknown): value is number => Number.isSafeInteger(value) && (value as number) > 0;
const event = (value: unknown): value is PageEvent => record(value) && typeof value.name === "string" && value.name.length <= 64 && "data" in value;
const events = (value: unknown): value is PageEvent[] => Array.isArray(value) && value.length <= 128 && value.every(event);
const metadata = (value: unknown): value is PageLeaseMetadata & Record<string, unknown> => record(value) && typeof value.claim === "string" && value.claim.length > 0 && value.claim.length <= 256 &&
  generation(value.rendererGeneration) && generation(value.bindingGeneration) && generation(value.documentEpoch);

function delivery(value: unknown): value is PageDelivery | PageLifecycle {
  if (!record(value)) return false;
  if (!metadata(value) || !sequence(value["sequence"])) return false;
  if (value.type === "pageEvent") return event(value.event);
  if (value.type !== "pageLifecycle") return false;
  return value.action === "reset" ? value.reason === "document" || value.reason === "provisional" : value.action === "snapshot" && events(value.events);
}

function appEvent(value: PageEvent): PageEvent {
  if (value.name !== "navigation" || !record(value.data)) return value;
  const data = value.data;
  return { ...value, data: { ...data,
    ...(typeof data.url === "string" ? { url: toAppUrl(data.url) } : {}),
    ...(typeof data.committedUrl === "string" ? { committedUrl: toAppUrl(data.committedUrl) } : {}),
  } };
}

export class HostPageEvents {
  private lease: PageLeaseMetadata | undefined;
  private watermark = 0;
  private early: (PageDelivery | PageLifecycle)[] = [];
  private active = true;
  private installed = false;
  private applying = false;
  private boundary = 0;
  private provisional = false;
  private pending: (PageDelivery | PageLifecycle)[] = [];
  private claim: string;
  private nativeId: () => number | undefined;
  private props: () => WebViewProps;
  private reset: (reason: PageResetReason) => void;
  private applyTheme: (state: NavigationState) => void;

  constructor(
    claim: string,
    nativeId: () => number | undefined,
    props: () => WebViewProps,
    reset: (reason: PageResetReason) => void,
    applyTheme: (state: NavigationState) => void,
  ) {
    this.claim = claim;
    this.nativeId = nativeId;
    this.props = props;
    this.reset = reset;
    this.applyTheme = applyTheme;
  }

  receive(module: string, id: string, payload: unknown): boolean {
    if (!this.active || module !== "ArdiaWebView" || !delivery(payload) || payload.claim !== this.claim) return false;
    if (!this.installed) {
      if (this.early.length >= 128) { this.dispose(); return false; }
      this.early.push(structuredClone({ ...payload, channel: id }) as PageDelivery | PageLifecycle);
      return true;
    }
    if (id !== String(this.nativeId())) return false;
    if (this.applying) {
      if (this.pending.length >= 128) { this.dispose(); return false; }
      this.pending.push(structuredClone(payload));
      return true;
    }
    const accepted = this.apply(payload);
    this.drain(this.pending.splice(0));
    return accepted;
  }

  install(value: unknown): boolean {
    if (!this.active || this.installed || !record(value) || !metadata(value.lease) || value.lease.claim !== this.claim ||
        !record(value.snapshot) || !sequence(value.snapshot.watermark) || !events(value.snapshot.events) ||
        !Number.isSafeInteger(this.nativeId()) || this.nativeId()! <= 0) return false;
    const lease = structuredClone(value.lease);
    const snapshot = structuredClone(value.snapshot) as PageCreationState["snapshot"];
    this.lease = lease;
    this.watermark = snapshot.watermark;
    this.boundary = snapshot.watermark;
    this.installed = true;
    this.applying = true;
    try {
      this.reset("mount");
      this.replay(snapshot.events);
    } finally { this.applying = false; }
    const buffered = [...this.early, ...this.pending];
    this.early = [];
    this.pending = [];
    this.drain(buffered.filter((item) => !("channel" in item) || item.channel === String(this.nativeId())));
    return this.active;
  }

  captureRequest(): (() => boolean) | null {
    if (!this.active || !this.lease || this.provisional) return null;
    const lease = this.lease;
    const boundary = this.boundary;
    return () => this.active && this.boundary === boundary && this.lease?.claim === lease.claim &&
      this.lease.rendererGeneration === lease.rendererGeneration && this.lease.bindingGeneration === lease.bindingGeneration &&
      this.lease.documentEpoch === lease.documentEpoch;
  }

  private drain(values: (PageDelivery | PageLifecycle)[]) {
    let processed = 0;
    while (values.length && this.active) {
      if (++processed > 256) { this.dispose(); return; }
      values.sort((a, b) => a.sequence - b.sequence);
      this.apply(values.shift()!);
      values.push(...this.pending.splice(0));
    }
  }

  private apply(value: PageDelivery | PageLifecycle): boolean {
    const lease = this.lease;
    if (!this.active || !lease || value.claim !== lease.claim || value.rendererGeneration !== lease.rendererGeneration ||
        value.bindingGeneration !== lease.bindingGeneration || value.sequence <= this.watermark) return false;
    if (BigInt(value.documentEpoch) < BigInt(lease.documentEpoch)) return false;
    if (value.type === "pageEvent" && value.documentEpoch !== lease.documentEpoch) return false;
    if (value.type === "pageEvent" && this.provisional && ["security", "popupBlocked", "pageMessage"].includes(value.event.name)) return false;
    if (value.type === "pageLifecycle" && value.action === "reset" && value.reason === "provisional" && value.documentEpoch !== lease.documentEpoch) return false;
    this.lease = { ...lease, documentEpoch: value.documentEpoch };
    this.watermark = value.sequence;
    if (value.type === "pageLifecycle") {
      this.boundary = value.sequence;
      this.provisional = value.action === "reset" && value.reason === "provisional";
    }
    this.applying = true;
    try {
      if (value.type === "pageEvent") this.replay([value.event]);
      else if (value.action === "reset") this.reset(value.reason);
      else { this.reset("snapshot"); this.replay(value.events); }
    } finally { this.applying = false; }
    return true;
  }

  private replay(values: PageEvent[]) {
    const popupIds = new Set<string>();
    for (const value of values) {
      if (!this.active) return;
      if (value.name === "popupBlocked" && record(value.data) && typeof value.data.id === "string") {
        if (popupIds.has(value.data.id)) continue;
        popupIds.add(value.data.id);
      }
      const presented = this.provisional && value.name === "navigation" && record(value.data)
        ? { ...value, data: { ...value.data, themeColor: null, themeColorSource: null } } : value;
      deliverPageEvent(this.props(), appEvent(presented));
      if (this.active && presented.name === "navigation") this.applyTheme(presented.data as NavigationState);
    }
  }

  dispose() {
    this.active = false;
    this.lease = undefined;
    this.early = [];
    this.pending = [];
  }
}
