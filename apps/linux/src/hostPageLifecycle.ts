import { patchPage, setPopover } from "../../browser/src/state/tabs/pageState";
import { updateLive } from "../../browser/src/state/tabs/actions";
import { revealToolbar } from "../../browser/src/state/windows/toolbarAutoHide";
import type { NavigationState } from "../../../packages/ardiacore/src/pageTypes";

export type PageResetReason = "mount" | "document" | "provisional" | "snapshot";

export function resetHostPage(tabId: string, _reason: PageResetReason) {
  patchPage(tabId, {
    security: null,
    themeColorSource: null,
    popups: [],
    status: "",
    crashed: null,
    unresponsive: false,
    fullscreen: false,
    blocked: 0,
    passwordPrompt: null,
    autofillPrompt: null,
  });
  setPopover(tabId, null);
  updateLive(tabId, { themeColor: null });
  revealToolbar(tabId);
}

export function applyHostPageTheme(tabId: string, navigation: NavigationState) {
  updateLive(tabId, { themeColor: navigation.themeColor });
  patchPage(tabId, { themeColorSource: navigation.themeColorSource });
}
