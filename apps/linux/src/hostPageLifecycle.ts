import { patchPage, setPopover } from "../../browser/src/state/tabs/pageState";
import { updateLive } from "../../browser/src/state/tabs/actions";
import { revealToolbar } from "../../browser/src/state/windows/toolbarAutoHide";
import type { NavigationState } from "../../../packages/ardiacore/src/pageTypes";

import type { PageState } from "../../browser/src/state/tabs/pageState";

export type PageResetReason = "mount" | "document" | "provisional" | "snapshot" | "restore";

const RESTORED: Record<string, Partial<PageState>> = {
  security: { security: null },
  navigation: { themeColorSource: null },
  popupBlocked: { popups: [] },
  status: { status: "" },
  crashed: { crashed: null },
  unresponsive: { unresponsive: false },
  responsive: { unresponsive: false },
  fullscreen: { fullscreen: false },
  contentBlocked: { blocked: 0 },
  passwordPrompt: { passwordPrompt: null },
  autofillPrompt: { autofillPrompt: null },
  mediaAccess: { mediaAccess: null },
};

export function resetHostPage(tabId: string, reason: PageResetReason, replaced: ReadonlySet<string> = new Set()) {
  if (reason === "provisional" || reason === "restore") {
    const kinds = reason === "provisional" ? new Set(["security", "navigation"]) : replaced;
    const patch: Partial<PageState> = {};
    for (const kind of kinds) Object.assign(patch, RESTORED[kind]);
    patchPage(tabId, patch);
    if (kinds.has("navigation")) updateLive(tabId, { themeColor: null });
    return;
  }
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
