import { hostSupports, invokeHost, reportHostError, type ArdiaHost } from "./host";

const FIELD_WAIT_MS = 500;
const TEXT_INPUTS = new Set(["", "text", "search", "url", "email", "tel", "password", "number"]);

type Focused = { tagName?: string; type?: string; isContentEditable?: boolean } | null | undefined;

const typingTarget = (element: Focused) => {
  if (!element) return false;
  if (element.isContentEditable) return true;
  const tag = element.tagName?.toUpperCase();
  return tag === "TEXTAREA" || (tag === "INPUT" && TEXT_INPUTS.has((element.type ?? "").toLowerCase()));
};

export function announceTypeaheadReady(host: ArdiaHost, {
  frame = (callback: () => void) => void requestAnimationFrame(callback),
  now = () => performance.now(),
  active = (): Focused => document.activeElement as Focused,
}: { frame?: (callback: () => void) => void; now?: () => number; active?: () => Focused } = {}) {
  if (!hostSupports("ArdiaShell.typeaheadReady", host)) return;
  const started = now();
  const check = () => {
    const field = typingTarget(active());
    if (!field && now() - started < FIELD_WAIT_MS) return frame(check);
    void invokeHost("ArdiaShell.typeaheadReady", { args: [field] }, host).catch(reportHostError);
  };
  frame(check);
}
