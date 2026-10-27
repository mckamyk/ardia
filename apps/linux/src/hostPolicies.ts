type InputTarget = Pick<EventTarget, "addEventListener" | "removeEventListener">;
type ModuleProvider = (name: string, method: string) => unknown;

const INPUT_EVENTS = ["keydown", "pointerdown", "pointermove", "wheel", "touchstart"] as const;

export function watchInputIdle(target: InputTarget, now: () => number = Date.now) {
  let last = now();
  const touch = () => { last = now(); };
  for (const type of INPUT_EVENTS) target.addEventListener(type, touch, { capture: true, passive: true });
  return {
    seconds: () => Math.max(0, (now() - last) / 1000),
    note: touch,
    stop: () => { for (const type of INPUT_EVENTS) target.removeEventListener(type, touch, { capture: true }); },
  };
}

export type LinuxPolicyDeps = {
  target: InputTarget;
  provide: (provider: ModuleProvider) => () => void;
  startLifecycle: () => void;
  startTrim: () => void;
  now?: () => number;
};

let running: (() => void) | null = null;

export function startLinuxTabPolicies(deps: LinuxPolicyDeps): () => void {
  if (running) return running;
  const idle = watchInputIdle(deps.target, deps.now);
  const unprovide = deps.provide((name, method) => name === "ArdiaEngine" && method === "inputIdleSeconds" ? () => idle.seconds() : undefined);
  deps.startLifecycle();
  deps.startTrim();
  const stop = () => {
    idle.stop();
    unprovide();
  };
  running = stop;
  return stop;
}
