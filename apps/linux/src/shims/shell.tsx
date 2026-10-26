import type { CommandEvent } from "@ardia-real/shell";
import { emitCommand as emitWebCommand } from "./shellBase";
import { ardiaHost, hostModuleMethod } from "../host";
import { isUtilityWindowId } from "../../../browser/src/components/settings/windows";
import { flushLinuxSession } from "../session";
import { createUtilityWindowCloser } from "../utilityClose";

export * from "./shellBase";
export { menuShown, showMenu, watchMenus } from "../menus";

export const emitCommand = (event: CommandEvent) => emitWebCommand(isUtilityWindowId(ardiaHost?.bootstrap.windowId)
  ? { ...event, windowId: ardiaHost!.bootstrap.windowId }
  : event);

export const closeWindow = createUtilityWindowCloser(flushLinuxSession, (id) => hostModuleMethod("ArdiaShell", "closeWindow")(id) as Promise<void>);
