import type { ConfirmOptions, PromptOptions } from "../../../packages/shell/src/dialogs";
import { ardiaHost, invokeHost, type ArdiaHost } from "./host";

type Confirmation = { confirmed: boolean; suppressed: boolean };
type DialogResult = Confirmation | string | null;
type PendingDialog = {
  kind: "confirm" | "prompt";
  suppression: boolean;
  accepted: boolean;
  received: boolean;
  result?: DialogResult;
  resolve(result: DialogResult): void;
  reject(error: unknown): void;
};

const MAX_PENDING = 16;
const decimal = (value: unknown): value is string => typeof value === "string" && /^[1-9][0-9]{0,19}$/.test(value);
const failure = (message: string) => Object.assign(new Error(message), { code: "ERR_DIALOG" });
const canceled = (kind: PendingDialog["kind"]): DialogResult => kind === "confirm" ? { confirmed: false, suppressed: false } : null;

export function createHostDialogs(host: ArdiaHost, newId = () => `dialog:${crypto.randomUUID()}`) {
  const windowId = host.bootstrap.windowId;
  const generation = host.bootstrap.nativeModules?.ArdiaShell?.rendererGeneration;
  const pending = new Map<string, PendingDialog>();
  let retired = false;
  let sequence = 0;

  const cancelNative = (id: string) => { void invokeHost("ArdiaShell.cancelDialog", { args: [id] }, host).catch(() => {}); };
  const finish = (id: string, entry: PendingDialog) => {
    if (pending.get(id) !== entry || !entry.accepted || !entry.received) return;
    pending.delete(id);
    entry.resolve(entry.result!);
  };
  const unsubscribe = host.onEvent((module, event, payload) => {
    if (module !== "ArdiaShell" || event !== "shell" || retired) return;
    if (host.bootstrap.nativeModules?.ArdiaShell?.rendererGeneration !== generation) { retire(); return; }
    const envelope = payload as { name?: string; data?: Record<string, unknown> } | null;
    if (envelope?.name !== "shell.dialogResolved") return;
    const data = envelope.data;
    if (!data || data.windowId !== windowId || data.rendererGeneration !== generation || !decimal(data.rendererGeneration) || typeof data.dialogId !== "string") return;
    const entry = pending.get(data.dialogId);
    if (!entry || entry.received) return;
    const value = data.result;
    if (entry.kind === "confirm") {
      const result = value as Partial<Confirmation> | null;
      if (!result || typeof result.confirmed !== "boolean" || typeof result.suppressed !== "boolean") return;
      entry.result = { confirmed: result.confirmed, suppressed: entry.suppression && result.suppressed };
    } else {
      if (value !== null && typeof value !== "string") return;
      entry.result = typeof value === "string" ? value.trim() || null : null;
    }
    entry.received = true;
    finish(data.dialogId, entry);
  });
  let removeBeforeClose: (() => void) | undefined;
  const retire = () => {
    if (retired) return;
    retired = true;
    unsubscribe();
    removeBeforeClose?.();
    const entries = [...pending];
    pending.clear();
    for (const [id, entry] of entries) {
      cancelNative(id);
      entry.resolve(canceled(entry.kind));
    }
  };
  removeBeforeClose = host.onBeforeClose?.(retire);

  const begin = (kind: PendingDialog["kind"], options: ConfirmOptions | PromptOptions): Promise<DialogResult> => {
    if (host.bootstrap.nativeModules?.ArdiaShell?.rendererGeneration !== generation) retire();
    if (retired || !decimal(generation)) return Promise.reject(failure("The dialog renderer is unavailable"));
    if (!options || typeof options.title !== "string" || options.title.length > 4096 || (options.windowId !== undefined && options.windowId !== windowId)) return Promise.reject(failure("Invalid dialog owner or title"));
    if (pending.size >= MAX_PENDING) return Promise.reject(failure("Too many pending dialogs"));
    if (sequence === Number.MAX_SAFE_INTEGER) return Promise.reject(failure("Dialog identities exhausted"));
    const id = `${newId()}:${++sequence}`;
    if (!/^[A-Za-z0-9:_-]{16,96}$/.test(id) || pending.has(id)) return Promise.reject(failure("Invalid or duplicate dialog identity"));
    return new Promise((resolve, reject) => {
      const entry: PendingDialog = { kind, suppression: kind === "confirm" && typeof (options as ConfirmOptions).suppression === "string", accepted: false, received: false, resolve, reject };
      pending.set(id, entry);
      void invokeHost("ArdiaShell.beginDialog", { args: [{ dialogId: id, kind, options: { ...options } }] }, host).then((reply) => {
        if (pending.get(id) !== entry) return;
        if (!reply || typeof reply !== "object" || !("accepted" in reply) || reply.accepted !== true) throw failure("The native dialog was not accepted");
        entry.accepted = true;
        finish(id, entry);
      }).catch((error: unknown) => {
        if (pending.get(id) !== entry) return;
        pending.delete(id);
        cancelNative(id);
        entry.reject(error);
      });
    });
  };
  return {
    confirm: (options: ConfirmOptions) => begin("confirm", options) as Promise<Confirmation>,
    prompt: (options: PromptOptions) => begin("prompt", options) as Promise<string | null>,
    retire,
  };
}

let dialogs: ReturnType<typeof createHostDialogs> | undefined;
export const linuxHostDialogs = () => dialogs ??= createHostDialogs(ardiaHost!);
