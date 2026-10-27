import type { ArdiaHost } from "./host";
import { invokeHost } from "./host.ts";

export const SYNC_CAPABILITIES = [
  "ArdiaSync.folderInfo",
  "ArdiaSync.chooseFolder",
  "ArdiaSync.deviceName",
  "ArdiaSync.status",
  "ArdiaSync.createPhrase",
  "ArdiaSync.enterPhrase",
  "ArdiaSync.unlock",
  "ArdiaSync.forget",
  "ArdiaSync.chainStatus",
  "ArdiaSync.createChain",
  "ArdiaSync.write",
  "ArdiaSync.read",
  "ArdiaSync.remove",
  "ArdiaSync.deleteChain",
  "ArdiaSync.sealLocal",
  "ArdiaSync.openLocal",
  "ArdiaSync.recoveryWords",
  "ArdiaSync.qrCode",
  "ArdiaSync.copyRecoveryPhrase",
  "ArdiaSync.saveRecoveryKit",
  "ArdiaSync.recoveryKitPreview",
  "ArdiaSync.copyRecoveryKit",
  "ArdiaSync.shareRecoveryKit",
  "ArdiaSync.revealFolder",
  "ArdiaSync.readPasswords",
  "ArdiaSync.savePassword",
  "ArdiaSync.claimAuthority",
  "ArdiaSync.releaseAuthority",
] as const;

export type SyncKeyStoreStatus = {
  keyStore: "secretService" | "file" | string;
  keyStoreStatus: "ok" | "notFound" | "locked" | "unavailable";
  lastKeyStatus: "ok" | "notFound" | "locked" | "unavailable";
  unlocked: boolean;
  unreadable: boolean;
  passwords: boolean;
};

export type AuthorityClaim = { granted: boolean; leaseMs: number };

const BASE32 = "abcdefghijklmnopqrstuvwxyz234567";
const FALLBACK_DEVICE_NAME = "Linux";
const SYNC_METHODS: ReadonlySet<string> = new Set(SYNC_CAPABILITIES);

export function base32(bytes: Uint8Array): string {
  let out = "";
  let buffer = 0;
  let bits = 0;
  for (const byte of bytes) {
    buffer = (buffer << 8) | byte;
    bits += 8;
    while (bits >= 5) {
      out += BASE32[(buffer >> (bits - 5)) & 31];
      bits -= 5;
    }
    buffer &= (1 << bits) - 1;
  }
  if (bits > 0) out += BASE32[(buffer << (5 - bits)) & 31];
  return out;
}

export function newSyncDeviceId(random: (bytes: Uint8Array) => Uint8Array = (bytes) => crypto.getRandomValues(bytes)): string {
  return base32(random(new Uint8Array(16)));
}

let deviceName: string | null = null;
let chosenFolder: { path: string } | null = null;

export const syncDeviceName = () => deviceName ?? FALLBACK_DEVICE_NAME;

export async function loadSyncDeviceName(host: ArdiaHost): Promise<string> {
  try {
    const name = await invokeHost<unknown>("ArdiaSync.deviceName", { args: [] }, host);
    if (typeof name === "string" && name.trim()) deviceName = name.trim().slice(0, 128);
  } catch {}
  return syncDeviceName();
}

export async function withChosenSyncFolder<T>(path: string, run: () => Promise<T>): Promise<T> {
  chosenFolder = { path };
  try {
    return await run();
  } finally {
    chosenFolder = null;
  }
}

export function keyringMessage(status: SyncKeyStoreStatus | null, saving: boolean): string | null {
  const store = status?.keyStore;
  const state = saving ? status?.lastKeyStatus ?? status?.keyStoreStatus : status?.keyStoreStatus;
  if (store !== "secretService") return null;
  if (state === "unavailable")
    return saving
      ? "Couldn’t save the sync key: no keyring is running. Start a Secret Service keyring, like GNOME Keyring or KWallet, and try again."
      : "No keyring is running, so Ardia can’t read this device’s sync key. Start a Secret Service keyring, like GNOME Keyring or KWallet, or enter your recovery phrase.";
  if (state === "locked")
    return saving
      ? "Couldn’t save the sync key: your keyring is locked. Unlock it and try again."
      : "Your keyring is locked. Unlock it, or enter your recovery phrase to keep syncing.";
  return null;
}

export function presentSyncState<S extends { status: string; error: string | null }>(state: S, status: SyncKeyStoreStatus | null): S {
  if (state.status !== "locked") return state;
  const message = keyringMessage(status, false);
  return message ? { ...state, error: message } : state;
}

export const syncKeyStoreStatus = (host: ArdiaHost) => invokeHost<SyncKeyStoreStatus>("ArdiaSync.status", { args: [] }, host);
export const claimSyncAuthority = (host: ArdiaHost) => invokeHost<AuthorityClaim>("ArdiaSync.claimAuthority", { args: [] }, host);
export const releaseSyncAuthority = (host: ArdiaHost) => invokeHost<void>("ArdiaSync.releaseAuthority", { args: [] }, host);

const invoke = (host: ArdiaHost, method: string) => (...args: unknown[]) =>
  invokeHost(`ArdiaSync.${method}`, { args: args.map((arg) => (arg === undefined ? null : arg)) }, host);

export function syncModuleMethod(host: ArdiaHost, name: string, method: string): unknown {
  if (name !== "ArdiaSync") return undefined;
  if (method === "newDeviceId") return () => newSyncDeviceId();
  if (method === "deviceName") return syncDeviceName;
  if (method === "chooseFolder") {
    return (current: string | null) => {
      const preset = chosenFolder;
      if (preset) {
        chosenFolder = null;
        return Promise.resolve(preset.path);
      }
      return invoke(host, method)(current ?? null);
    };
  }
  if (method === "readPasswords") return () => Promise.resolve(null);
  if (method === "savePassword") return () => Promise.resolve(false);
  if (SYNC_METHODS.has(`ArdiaSync.${method}`)) return invoke(host, method);
  return undefined;
}
