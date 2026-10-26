import type { TabViewport } from "./hostTab";

type Bounds = { x: number; y: number; width: number; height: number };

let overlayBlocked = (_key: string | undefined) => false;
export const hostOverlayBlocked = (key: string | undefined) => overlayBlocked(key);
export const setHostOverlayCheck = (check: typeof overlayBlocked) => void (overlayBlocked = check);

export function tabViewport(rect: Bounds, viewport: { width: number; height: number }, visible: boolean): TabViewport {
  const finite = [rect.x, rect.y, rect.width, rect.height].every(Number.isFinite);
  const intersects = finite && rect.width > 0 && rect.height > 0 && rect.x < viewport.width && rect.y < viewport.height && rect.x + rect.width > 0 && rect.y + rect.height > 0;
  return { rect: finite ? { x: rect.x, y: rect.y, width: Math.max(0, rect.width), height: Math.max(0, rect.height) } : { x: 0, y: 0, width: 0, height: 0 }, visible: visible && intersects };
}

export function watchTabViewport(element: HTMLElement, visible: () => boolean, report: (viewport: TabViewport) => void) {
  let frame = 0;
  let stopped = false;
  const measure = () => {
    if (stopped) return;
    const rect = element.getBoundingClientRect();
    const viewport = tabViewport(rect, { width: window.innerWidth, height: window.innerHeight }, visible() && !document.hidden && element.isConnected);
    if (viewport.visible) {
      for (let parent: HTMLElement | null = element; parent; parent = parent.parentElement) {
        const style = getComputedStyle(parent);
        if (style.display === "none" || style.visibility === "hidden" || Number(style.opacity) === 0) {
          viewport.visible = false;
          break;
        }
      }
    }
    if (viewport.visible) {
      const insetX = Math.min(16, rect.width / 4);
      const insetY = Math.min(16, rect.height / 4);
      const left = Math.max(0, rect.left) + insetX;
      const top = Math.max(0, rect.top) + insetY;
      const right = Math.min(window.innerWidth, rect.right) - insetX;
      const bottom = Math.min(window.innerHeight, rect.bottom) - insetY;
      const points = [[left, top], [right, top], [left, bottom], [right, bottom], [(left + right) / 2, (top + bottom) / 2]];
      viewport.visible = points.every(([x, y]) => {
        const hit = document.elementFromPoint(x!, y!);
        return !!hit && (hit === element || element.contains(hit));
      });
    }
    report(viewport);
  };
  const tick = () => {
    measure();
    if (!stopped) frame = requestAnimationFrame(tick);
  };
  const observer = new ResizeObserver(measure);
  observer.observe(element);
  window.addEventListener("resize", measure);
  window.addEventListener("scroll", measure, true);
  document.addEventListener("visibilitychange", measure);
  window.visualViewport?.addEventListener("resize", measure);
  window.visualViewport?.addEventListener("scroll", measure);
  tick();
  return () => {
    stopped = true;
    cancelAnimationFrame(frame);
    observer.disconnect();
    window.removeEventListener("resize", measure);
    window.removeEventListener("scroll", measure, true);
    document.removeEventListener("visibilitychange", measure);
    window.visualViewport?.removeEventListener("resize", measure);
    window.visualViewport?.removeEventListener("scroll", measure);
  };
}
