import type { Download } from "../../../packages/ardiacore/src/module";
import type { ArdiaHost } from "./host";
import { callHost, reportHostError } from "./host.ts";
import { hostFiles } from "./hostFiles.ts";

const DOWNLOAD_ID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const STATES = new Set(["downloading", "finished", "failed", "cancelled"]);
export const DOWNLOAD_LIMIT = 100;

export const nativeDownloadId = (id: unknown): id is string => typeof id === "string" && DOWNLOAD_ID.test(id);

const text = (value: unknown) => typeof value === "string";
const count = (value: unknown) => typeof value === "number" && Number.isFinite(value);

export function snapshotDownload(value: unknown): Download | null {
  if (!value || typeof value !== "object") return null;
  const d = value as Record<string, unknown>;
  if (!nativeDownloadId(d.id) || !STATES.has(d.state as string) || d.offTheRecord === true) return null;
  if (![d.url, d.filename, d.path, d.mimeType].every(text) || typeof d.paused !== "boolean") return null;
  if (![d.received, d.total, d.speed].every(count)) return null;
  return { ...(d as Download), profile: "" };
}

export function mergeDownloadSnapshot(current: Download[], snapshot: unknown, limit = DOWNLOAD_LIMIT): Download[] | null {
  if (!Array.isArray(snapshot)) return null;
  const known = new Set(current.map((d) => d.id));
  const added: Download[] = [];
  for (const entry of snapshot) {
    const d = snapshotDownload(entry);
    if (!d || known.has(d.id)) continue;
    known.add(d.id);
    added.push(d);
  }
  if (!added.length || current.length >= limit) return null;
  return [...current, ...added].slice(0, limit);
}

type DownloadState = {
  downloads: Download[];
  removeDownload(id: string): void;
  clearDownloads(windowId?: string): void;
};

export type DownloadStore = {
  getState(): DownloadState;
  setState(patch: Partial<DownloadState>): void;
};

const removable = (d: Download) => nativeDownloadId(d.id) && d.state !== "downloading";

export function startHostDownloads(host: ArdiaHost, store: DownloadStore) {
  const { removeDownload, clearDownloads } = store.getState();
  const request = (verb: string, id: string) => callHost(`downloads.${verb}`, "", JSON.stringify({ id }), host);
  const forget = (gone: Download[]) => {
    for (const d of gone.filter(removable)) {
      void request("remove", d.id)
        .catch((error) => (d.state === "failed" ? request("cancel", d.id).then(() => request("remove", d.id)) : Promise.reject(error)))
        .catch(reportHostError);
    }
  };
  store.setState({
    removeDownload(id) {
      const gone = store.getState().downloads.filter((d) => d.id === id);
      removeDownload(id);
      forget(gone);
    },
    clearDownloads(windowId) {
      const before = store.getState().downloads;
      clearDownloads(windowId);
      const kept = new Set(store.getState().downloads.map((d) => d.id));
      forget(before.filter((d) => !kept.has(d.id)));
    },
  });
  const stop = host.onEvent((module, event, payload) => {
    if (module !== "ArdiaDownloads" || event !== "downloads" || !payload || typeof payload !== "object") return;
    const { name, data } = payload as { name?: unknown; data?: { id?: unknown } | null };
    if (name !== "downloads.removed" || !nativeDownloadId(data?.id)) return;
    if (store.getState().downloads.some((d) => d.id === data.id)) removeDownload(data.id);
  });
  const stopFiles = hostFiles(host).onChange(() => store.setState({ downloads: [...store.getState().downloads] }));
  const ready = callHost("downloads.list", "", "{}", host)
    .then((reply) => {
      const merged = mergeDownloadSnapshot(store.getState().downloads, JSON.parse(reply));
      if (merged) store.setState({ downloads: merged });
    })
    .catch(reportHostError);
  return {
    ready,
    stop() {
      stop();
      stopFiles();
      store.setState({ removeDownload, clearDownloads });
    },
  };
}
