import type { CommandEvent } from "@ardia/shell";

export const BROWSER_COMMAND_CHANNEL = "ardia-browser-commands";
export const BROWSER_WINDOW_COMMANDS: ReadonlySet<string> = new Set(["showOnboarding", "toolTour", "videoTour", "releaseNotes", "sendFeedback"]);

type Command = CommandEvent["command"];
type RouteMessage =
  | { type: "find"; request: string }
  | { type: "here"; request: string; windowId: string; focusedAt: number }
  | { type: "run"; request: string; windowId: string; command: Command }
  | { type: "ran"; request: string; windowId: string };

export type RouteChannel = {
  postMessage(message: RouteMessage): void;
  addEventListener(type: "message", listener: (event: { data: RouteMessage }) => void): void;
  removeEventListener(type: "message", listener: (event: { data: RouteMessage }) => void): void;
};

type FocusTarget = {
  addEventListener(type: "focus", listener: () => void): void;
  hasFocus(): boolean;
};

export function serveBrowserCommands(options: {
  windowId: string;
  channel: RouteChannel;
  target: FocusTarget;
  run: (windowId: string, command: Command) => void;
  focus: (windowId: string) => Promise<unknown>;
  now?: () => number;
}) {
  const { windowId, channel, target, run, focus } = options;
  const now = options.now ?? Date.now;
  let focusedAt = target.hasFocus() ? now() : 0;
  target.addEventListener("focus", () => { focusedAt = now(); });
  const listener = ({ data }: { data: RouteMessage }) => {
    if (data?.type === "find") channel.postMessage({ type: "here", request: data.request, windowId, focusedAt });
    if (data?.type !== "run" || data.windowId !== windowId || !BROWSER_WINDOW_COMMANDS.has(data.command)) return;
    channel.postMessage({ type: "ran", request: data.request, windowId });
    void focus(windowId).catch(() => {});
    run(windowId, data.command);
  };
  channel.addEventListener("message", listener);
  return { remove: () => channel.removeEventListener("message", listener) };
}

export function routeToBrowserWindow(options: {
  channel: RouteChannel;
  command: Command;
  request?: string;
  gatherMs?: number;
  answerMs?: number;
}): Promise<string | null> {
  const { channel, command } = options;
  const request = options.request ?? `${Date.now()}-${Math.random().toString(36).slice(2)}`;
  const gatherMs = options.gatherMs ?? 150;
  const answerMs = options.answerMs ?? 1500;
  return new Promise((resolve) => {
    const candidates: { windowId: string; focusedAt: number }[] = [];
    let chosen: string | null = null;
    let timer: ReturnType<typeof setTimeout>;
    const finish = (windowId: string | null) => {
      clearTimeout(timer);
      channel.removeEventListener("message", listener);
      resolve(windowId);
    };
    const listener = ({ data }: { data: RouteMessage }) => {
      if (data?.request !== request) return;
      if (data.type === "here" && !chosen) candidates.push({ windowId: data.windowId, focusedAt: data.focusedAt });
      if (data.type === "ran" && data.windowId === chosen) finish(chosen);
    };
    channel.addEventListener("message", listener);
    timer = setTimeout(() => {
      const best = candidates.reduce<{ windowId: string; focusedAt: number } | null>((top, next) => !top || next.focusedAt > top.focusedAt ? next : top, null);
      if (!best) return finish(null);
      chosen = best.windowId;
      timer = setTimeout(() => finish(null), answerMs);
      channel.postMessage({ type: "run", request, windowId: chosen, command });
    }, gatherMs);
    channel.postMessage({ type: "find", request });
  });
}
