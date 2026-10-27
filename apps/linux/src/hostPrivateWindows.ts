import type { ArdiaHost } from "./host";
import { invokeHost } from "./host.ts";

export type PrivateWindowEntry = { id: string; title: string };

const WINDOW_ID = /^[A-Za-z0-9][A-Za-z0-9_-]{0,127}$/;

export function privateWindowEntries(reply: unknown, own: string): PrivateWindowEntry[] {
  if (!Array.isArray(reply)) return [];
  const ids = new Set<string>();
  for (const entry of reply) {
    const id = entry && typeof entry === "object" ? (entry as { id?: unknown }).id : undefined;
    if (typeof id === "string" && WINDOW_ID.test(id) && id !== own) ids.add(id);
  }
  return [...ids].map((id, index) => ({ id, title: index ? `Private Window ${index + 1}` : "Private Window" }));
}

export function watchHostPrivateWindows(host: ArdiaHost, onChange: (windows: PrivateWindowEntry[]) => void) {
  let generation = 0;
  let stopped = false;
  const refresh = () => {
    const current = ++generation;
    const settle = (windows: PrivateWindowEntry[]) => {
      if (!stopped && current === generation) onChange(windows);
    };
    void invokeHost("ArdiaShell.privateWindows", { args: [] }, host).then(
      (reply) => settle(privateWindowEntries(reply, host.bootstrap.windowId)),
      () => settle([]),
    );
  };
  const unsubscribe = host.onEvent((module, event) => {
    if (module === "ArdiaShell" && event === "privateWindows") refresh();
  });
  refresh();
  return {
    focus: (id: string) => invokeHost("ArdiaShell.focusWindow", { args: [id] }, host),
    stop() {
      stopped = true;
      unsubscribe();
    },
  };
}
