import type { CommandEvent } from "@ardia-real/shell";
import { emitCommand as emitWebCommand } from "./shellBase";
import { ardiaHost, hostModuleMethod } from "../host";
import { flushLinuxSession } from "../session";
import { createUtilityWindowCloser } from "../utilityClose";
import { linuxMenuShortcuts } from "../shortcuts";
import { useBrowser } from "../../../browser/src/store/browser";

export * from "./shellBase";
export { menuShown, showMenu, watchMenus } from "../menus";
export { recordShortcut, cancelShortcutRecording } from "../shortcutsRuntime";

export const emitCommand = (event: CommandEvent) => emitWebCommand(ardiaHost
  ? { ...event, windowId: ardiaHost!.bootstrap.windowId }
  : event);

export const menuShortcuts = async () => linuxMenuShortcuts(useBrowser.getState().settings.shortcuts);

export const closeWindow = createUtilityWindowCloser(flushLinuxSession, (id) => hostModuleMethod("ArdiaShell", "closeWindow")(id) as Promise<void>);
