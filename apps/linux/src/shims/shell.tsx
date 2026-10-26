import type { CommandEvent } from "@ardia-real/shell";
import { emitCommand as emitWebCommand } from "./shellBase";
import { ardiaHost, hostModuleMethod, reportHostError } from "../host";
import { flushLinuxSession } from "../session";
import { createWindowCloseBarrier, type WindowCloseRequest } from "../utilityClose";
import { quiesceLinuxSession, resumeLinuxSession } from "../sessionClose";
import { quiesceLinuxDocuments, resumeLinuxDocuments, flushLinuxDocuments } from "../documentFlush";
import { linuxMenuShortcuts } from "../shortcuts";
import { useBrowser } from "../../../browser/src/state/browser/store";

export * from "./shellBase";
export { menuShown, showMenu, watchMenus } from "../menus";
export { recordShortcut, cancelShortcutRecording } from "../shortcutsRuntime";

export const emitCommand = (event: CommandEvent) => emitWebCommand(ardiaHost
  ? { ...event, windowId: ardiaHost!.bootstrap.windowId }
  : event);

export const menuShortcuts = async () => linuxMenuShortcuts(useBrowser.getState().settings.shortcuts);

const closeToken = (request: WindowCloseRequest) => JSON.stringify(request);
const closeBarrier = createWindowCloseBarrier({
  quiesce: (request) => { quiesceLinuxSession(closeToken(request)); quiesceLinuxDocuments(closeToken(request)); },
  drain: async () => { await flushLinuxSession(); return flushLinuxDocuments(); },
  acknowledge: (request, watermark) => hostModuleMethod("ArdiaShell", "acknowledgeWindowClose")({ ...request, watermark }) as Promise<void>,
  reject: (request) => hostModuleMethod("ArdiaShell", "rejectWindowClose")(request) as Promise<void>,
  resume: (request) => { resumeLinuxSession(closeToken(request)); resumeLinuxDocuments(closeToken(request)); },
});
const acceptCloseRequest = (request: WindowCloseRequest) => {
  if (request.windowId !== ardiaHost?.bootstrap.windowId) return Promise.resolve();
  return closeBarrier(request);
};
ardiaHost?.onEvent((module, event, payload) => {
  if (module !== "ArdiaShell" || event !== "shell") return;
  const envelope = payload as { name: string; data: WindowCloseRequest };
  if (envelope?.name === "shell.closeRequested") void acceptCloseRequest(envelope.data).catch(reportHostError);
});
export const closeWindow = async (id: string): Promise<void> => {
  const request = await hostModuleMethod("ArdiaShell", "requestWindowClose")(id) as WindowCloseRequest;
  await acceptCloseRequest(request);
};
