import { activeTabId } from "../../browser/src/state/browser/model";
import type { BrowserState } from "../../browser/src/state/browser/store";
import { hostSupports, invokeHost, reportHostError, type ArdiaHost } from "./host";

const FIELD_WAIT_MS = 500;
const EXPECTED_FIELD_WAIT_MS = 8000;
const IDLE_WAIT_MS = 2000;
const TEXT_INPUTS = new Set(["", "text", "search", "url", "email", "tel", "password", "number"]);

type Focused = { tagName?: string; type?: string; isContentEditable?: boolean } | null | undefined;

const typingTarget = (element: Focused) => {
  if (!element) return false;
  if (element.isContentEditable) return true;
  const tag = element.tagName?.toUpperCase();
  return tag === "TEXTAREA" || (tag === "INPUT" && TEXT_INPUTS.has((element.type ?? "").toLowerCase()));
};

export function windowExpectsField(s: Pick<BrowserState, "windows" | "tabs">, windowId: string) {
  const tabId = activeTabId(s as BrowserState, windowId);
  return !tabId || !s.tabs[tabId]?.url;
}

const whenIdle = (callback: () => void) => {
  if (typeof requestIdleCallback === "function") requestIdleCallback(callback, { timeout: IDLE_WAIT_MS });
  else setTimeout(callback, 0);
};

export function announceTypeaheadReady(host: ArdiaHost, {
  frame = (callback: () => void) => void requestAnimationFrame(callback),
  now = () => performance.now(),
  active = (): Focused => document.activeElement as Focused,
  idle = whenIdle,
  expectsField = () => false,
}: {
  frame?: (callback: () => void) => void;
  now?: () => number;
  active?: () => Focused;
  idle?: (callback: () => void) => void;
  expectsField?: () => boolean;
} = {}) {
  if (!hostSupports("ArdiaShell.typeaheadReady", host)) return;
  const started = now();
  const announce = (field: boolean) => void invokeHost("ArdiaShell.typeaheadReady", { args: [field] }, host).catch(reportHostError);
  const check = () => {
    if (typingTarget(active())) {
      idle(() => (typingTarget(active()) ? announce(true) : frame(check)));
      return;
    }
    const waited = now() - started;
    if (waited < FIELD_WAIT_MS || (waited < EXPECTED_FIELD_WAIT_MS && expectsField())) return frame(check);
    announce(false);
  };
  frame(check);
}
