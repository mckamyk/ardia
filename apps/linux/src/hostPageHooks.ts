import type { NavigationState } from "../../../packages/ardiacore/src/pageTypes";
import type { PageResetReason } from "./hostPageLifecycle";

type HostPageHooks = {
  reset(tabId: string, reason: PageResetReason): void;
  applyTheme(tabId: string, state: NavigationState): void;
};

let hooks: HostPageHooks | undefined;

export function setHostPageHooks(next: HostPageHooks) {
  hooks = next;
}

export function resetHostPageState(tabId: string, reason: PageResetReason) {
  hooks?.reset(tabId, reason);
}

export function applyHostPageThemeState(tabId: string, state: NavigationState) {
  hooks?.applyTheme(tabId, state);
}
