import type { CommandEvent } from "@ardia-real/shell";
import { emitCommand as emitWebCommand } from "./shellBase";
import { ardiaHost, hostModuleMethod, reportHostError } from "../host";
import { flushLinuxSession, isLinuxUtility } from "../session";
import { createWindowCloseBarrier, type WindowCloseRequest } from "../utilityClose";
import { quiesceLinuxSession, resumeLinuxSession } from "../sessionClose";
import { quiesceLinuxDocuments, resumeLinuxDocuments, flushLinuxDocuments } from "../documentFlush";
import { linuxMenuShortcuts } from "../shortcuts";
import { useBrowser } from "../../../browser/src/state/browser/store";
import { setManagedWindowClose } from "../../../browser/src/platform/windowClose";
import { createWindowCloseCoordinator, type DecideClose } from "../windowCloseCoordinator";

export * from "./shellBase";
export { menuShown, showMenu, watchMenus } from "../menus";
export { recordShortcut, cancelShortcutRecording } from "../shortcutsRuntime";

export const emitCommand = (event: CommandEvent) => emitWebCommand(ardiaHost
  ? { ...event, windowId: ardiaHost!.bootstrap.windowId }
  : event);

export const menuShortcuts = async () => linuxMenuShortcuts(useBrowser.getState().settings.shortcuts);

const closeToken = (request: WindowCloseRequest) => JSON.stringify(request);
const ownWindowId = ardiaHost?.bootstrap.windowId ?? "";
const closeBarrier = createWindowCloseBarrier({
  quiesce: (request) => { quiesceLinuxSession(closeToken(request)); quiesceLinuxDocuments(closeToken(request)); },
  drain: async () => { await flushLinuxSession(); return flushLinuxDocuments(); },
  acknowledge: (request, watermark) => hostModuleMethod("ArdiaShell", "acknowledgeWindowClose")({ ...request, watermark }) as Promise<void>,
  reject: (request) => hostModuleMethod("ArdiaShell", "rejectWindowClose")(request) as Promise<void>,
  resume: (request) => {
    resumeLinuxSession(closeToken(request));
    resumeLinuxDocuments(closeToken(request));
    closeCoordinator.producersResumed(request);
  },
});
let provideDecider: (decide: DecideClose) => void = () => {};
const decider = new Promise<DecideClose>((resolve) => { provideDecider = resolve; });
export const setWindowCloseDecider = (decide: DecideClose) => provideDecider(decide);
const decideClose: DecideClose = (windowId, reason, ask) => decider.then((decide) => decide(windowId, reason, ask));
const closeCoordinator = createWindowCloseCoordinator({
  windowId: ownWindowId,
  utility: isLinuxUtility(ownWindowId),
  decide: decideClose,
  suppress: (reason) => useBrowser.getState().updateSettings(reason === "lastTab" ? { warnBeforeClosingLastTab: false } : { warnBeforeClosingWindow: false }),
  flush: flushLinuxSession,
  request: (id) => hostModuleMethod("ArdiaShell", "requestWindowClose")(id) as Promise<WindowCloseRequest>,
  beginDrain: (request) => hostModuleMethod("ArdiaShell", "beginWindowCloseDrain")(request),
  reject: (request) => hostModuleMethod("ArdiaShell", "rejectWindowClose")(request) as Promise<void>,
  barrier: closeBarrier,
  cancelled: closeBarrier.cancelled,
  report: reportHostError,
});
const reportClose = (error: unknown) => {
  if ((error as { code?: string } | null)?.code !== "ERR_WINDOW_CLOSE_CANCELLED") reportHostError(error);
};
ardiaHost?.onEvent((module, event, payload) => {
  if (module !== "ArdiaShell" || event !== "shell") return;
  const envelope = payload as { name: string; data: WindowCloseRequest };
  if (envelope?.name === "shell.closeRequested") void closeCoordinator.accept(envelope.data).catch(reportClose);
  if (envelope?.name === "shell.closeCancelled" && envelope.data?.windowId === ownWindowId)
    closeCoordinator.cancelled(envelope.data);
});
if (ardiaHost && !isLinuxUtility(ownWindowId)) setManagedWindowClose((windowId, reason) => {
  if (windowId !== ownWindowId) return false;
  void closeCoordinator.close(reason).catch(reportClose);
  return true;
});
export const closeWindow = async (id: string): Promise<void> => {
  if (ardiaHost && id === ownWindowId) return closeCoordinator.close("window");
  const request = await hostModuleMethod("ArdiaShell", "requestWindowClose")(id) as WindowCloseRequest;
  await closeCoordinator.accept(request);
};
