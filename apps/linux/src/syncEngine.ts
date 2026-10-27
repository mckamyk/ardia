import { create } from "zustand";
import * as engine from "../../browser/src/state/sync/engine";
import { DOC, JOURNAL, STATE, type DataType, type SyncUi } from "../../browser/src/state/sync/syncDoc";
import type { FolderInfo, PhraseResult } from "../../browser/src/platform/sync";
import { SyncNative } from "../../browser/src/platform/sync";
import { ardiaHost, type ArdiaHost } from "./host";
import {
  claimSyncAuthority,
  keyringMessage,
  loadSyncDeviceName,
  presentSyncState,
  releaseSyncAuthority,
  syncDeviceName,
  syncKeyStoreStatus,
  withChosenSyncFolder,
  type SyncKeyStoreStatus,
} from "./hostSync.ts";
import { refreshDocument } from "./shims/shellBase";
import {
  INTERACTIVE_CALL_TIMEOUT_MS,
  SYNC_CHANNEL,
  startSyncAuthority,
  type LocksLike,
  type RemoteMethod,
  type SyncAuthority,
  type SyncRemote,
} from "./syncAuthority.ts";

const MAX_JOURNAL_PARTS = 1000;
const KEYCHAIN_ERROR = "Couldn’t save the sync key in your Keychain.";

export const useSync = create<SyncUi>()(() => ({ ...engine.useSync.getState(), status: SyncNative ? "starting" : "unavailable" }));

let authority: SyncAuthority | null = null;
let keyStore: SyncKeyStoreStatus | null = null;

async function refreshSyncDocuments() {
  for (const name of [DOC, STATE, JOURNAL]) await refreshDocument(name);
  for (let n = 1; n <= MAX_JOURNAL_PARTS; n++) {
    if (!(await refreshDocument(`sync-journal-${n}.nns`))) break;
  }
}

function remoteFor(host: ArdiaHost): SyncRemote {
  return {
    syncNow: () => engine.syncNow(),
    turnOnSync: async (folder: string) => {
      const result = await engine.turnOnSync(folder);
      if ("error" in result && result.error === KEYCHAIN_ERROR) {
        keyStore = await syncKeyStoreStatus(host).catch(() => keyStore);
        return { error: keyringMessage(keyStore, true) ?? "Couldn’t save the sync key in your keyring." };
      }
      return result;
    },
    enterRecoveryPhrase: (folder: string, text: string) => engine.enterRecoveryPhrase(folder, text),
    stopSync: (options?: { deleteData?: boolean }) => engine.stopSync(options),
    setTypeSynced: (type: DataType, enabled: boolean) => engine.setTypeSynced(type, enabled),
    setProfileSynced: (profileId: string, enabled: boolean) => engine.setProfileSynced(profileId, enabled),
    addRemoteProfile: (syncId: string) => engine.addRemoteProfile(syncId),
    adoptProfileTheme: (profileId: string) => engine.adoptProfileTheme(profileId),
    useFolder: (path: string) => withChosenSyncFolder(path, () => engine.chooseSyncFolder()),
  };
}

async function becomeAuthority(host: ArdiaHost) {
  try {
    await refreshSyncDocuments();
  } catch (error) {
    useSync.setState({ status: "stalled", error: "Ardia couldn’t read this device’s sync files, so sync is paused to keep them." });
    throw error;
  }
  await loadSyncDeviceName(host);
  keyStore = await syncKeyStoreStatus(host).catch(() => null);
  engine.startSync();
  return { store: engine.useSync, remote: remoteFor(host) };
}

export function startLinuxSync(options: { host?: ArdiaHost; candidate: boolean; locks?: LocksLike; channel?: () => BroadcastChannel | null }) {
  const host = options.host ?? ardiaHost;
  if (authority || !SyncNative || !host) return authority;
  const channel = (options.channel ?? (() => (typeof BroadcastChannel === "undefined" ? null : new BroadcastChannel(SYNC_CHANNEL))))();
  const id = `${host.bootstrap.windowId}:${Math.random().toString(36).slice(2)}`;
  const started = startSyncAuthority<SyncUi>({
    id,
    candidate: options.candidate && !host.bootstrap.privateWindow,
    locks: options.locks,
    channel,
    mirror: useSync,
    claim: () => claimSyncAuthority(host),
    release: () => releaseSyncAuthority(host),
    become: () => becomeAuthority(host),
    present: (state) => presentSyncState(state, keyStore),
  });
  authority = started;
  if (typeof addEventListener === "function") addEventListener("pagehide", () => started.stop(), { once: true });
  return started;
}

export const linuxSyncHooks = {
  state: () => useSync.getState(),
  role: () => authority?.role() ?? "unavailable",
  turnOnSync: (folder: string) => turnOnSync(folder),
  enterRecoveryPhrase: (folder: string, text: string) => enterRecoveryPhrase(folder, text),
  stopSync: (options?: { deleteData?: boolean }) => stopSync(options),
  syncNow: () => syncNow(),
  setTypeSynced: (type: DataType, enabled: boolean) => setTypeSynced(type, enabled),
};

export const startSync = () => void startLinuxSync({ candidate: true, locks: typeof navigator === "undefined" ? undefined : navigator.locks });

const leading = () => !!authority?.isAuthority();

function forward<T>(method: RemoteMethod, args: unknown[], timeoutMs?: number): Promise<T> {
  if (!authority) return Promise.reject(new Error("Sync isn’t running in this window."));
  return authority.call(method, args, timeoutMs) as Promise<T>;
}

const quietly = (promise: Promise<unknown>) => void promise.catch((error: unknown) => console.warn("[ardia linux] sync", error));

export const currentSyncFolder = () => useSync.getState().folder;
export const deviceName = () => syncDeviceName();
export const appScopeRecords = () => (leading() ? engine.appScopeRecords() : {});

export async function folderInfo(path?: string | null): Promise<FolderInfo | null> {
  return (await SyncNative?.folderInfo(path ?? useSync.getState().folder ?? null)) ?? null;
}

export async function chooseSyncFolder(): Promise<string | null> {
  if (!SyncNative) return null;
  if (leading()) return engine.chooseSyncFolder();
  const picked = await SyncNative.chooseFolder(useSync.getState().folder);
  if (!picked) return null;
  await forward("useFolder", [picked]);
  return picked;
}

export async function syncNow() {
  await forward("syncNow", [], INTERACTIVE_CALL_TIMEOUT_MS);
}

export async function turnOnSync(folder: string): Promise<{ ok: true } | { error: string }> {
  if (!SyncNative) return { error: "Sync isn’t available in this build." };
  try {
    return await forward("turnOnSync", [folder], INTERACTIVE_CALL_TIMEOUT_MS);
  } catch (error) {
    return { error: error instanceof Error ? error.message : String(error) };
  }
}

export async function enterRecoveryPhrase(folder: string, text: string): Promise<PhraseResult | { error: "unavailable" }> {
  if (!SyncNative) return { error: "unavailable" };
  try {
    return await forward("enterRecoveryPhrase", [folder, text], INTERACTIVE_CALL_TIMEOUT_MS);
  } catch (error) {
    console.warn("[ardia linux] sync", error instanceof Error ? error.message : error);
    return { error: "unavailable" };
  }
}

export async function stopSync(options: { deleteData?: boolean } = {}) {
  await forward("stopSync", [options], INTERACTIVE_CALL_TIMEOUT_MS);
}

export function setTypeSynced(type: DataType, enabled: boolean) {
  quietly(forward("setTypeSynced", [type, enabled]));
}

export function setProfileSynced(profileId: string, enabled: boolean) {
  quietly(forward("setProfileSynced", [profileId, enabled]));
}

export function addRemoteProfile(syncId: string): string | null {
  if (leading()) return engine.addRemoteProfile(syncId);
  quietly(forward("addRemoteProfile", [syncId]));
  return null;
}

export function adoptProfileTheme(profileId: string) {
  if (leading()) return engine.adoptProfileTheme(profileId);
  quietly(forward("adoptProfileTheme", [profileId]));
}
