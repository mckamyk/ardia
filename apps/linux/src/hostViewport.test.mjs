import assert from "node:assert/strict";
import test from "node:test";
import { watchTabViewport } from "./hostViewport.ts";

test("viewport observation tracks transforms, scroll, visual zoom, overlays, and document visibility", () => {
  const previous = Object.fromEntries(["window", "document", "getComputedStyle", "ResizeObserver", "requestAnimationFrame", "cancelAnimationFrame"].map((key) => [key, globalThis[key]]));
  const windowEvents = new EventTarget();
  const visualViewport = new EventTarget();
  const documentEvents = new EventTarget();
  let rect = { x: 200, y: 50, width: 800, height: 600, left: 200, top: 50, right: 1000, bottom: 650 };
  let frame;
  let blocked = false;
  let cards = [];
  let cardAtSample = false;
  let disconnected = false;
  const element = { isConnected: true, parentElement: null, getBoundingClientRect: () => rect, contains: () => false };
  const document = Object.assign(documentEvents, { hidden: false, querySelectorAll: () => cards, elementFromPoint: () => blocked ? {} : cardAtSample ? cards[0] : element });
  globalThis.window = Object.assign(windowEvents, { innerWidth: 1280, innerHeight: 800, visualViewport });
  globalThis.document = document;
  globalThis.getComputedStyle = () => ({ display: "block", visibility: "visible", opacity: "1" });
  globalThis.ResizeObserver = class { observe() {} disconnect() { disconnected = true; } };
  globalThis.requestAnimationFrame = (callback) => { frame = callback; return 1; };
  globalThis.cancelAnimationFrame = () => {};
  const updates = [];
  try {
    const stop = watchTabViewport(element, () => true, (update) => updates.push(update));
    assert.equal(updates.at(-1).visible, true);
    rect = { ...rect, x: 250, left: 250, right: 1050 };
    windowEvents.dispatchEvent(new Event("scroll"));
    assert.equal(updates.at(-1).rect.x, 250);
    rect = { ...rect, width: 720, right: 970 };
    visualViewport.dispatchEvent(new Event("resize"));
    assert.equal(updates.at(-1).rect.width, 720);
    cards = [{ getBoundingClientRect: () => ({ x: 192, y: 48, left: 192, top: 48, right: 456, bottom: 160, width: 264, height: 112 }), contains: () => false }];
    cardAtSample = true;
    frame();
    assert.equal(updates.at(-1).visible, true);
    assert.deepEqual(updates.at(-1).occlusions, [{ x: 192, y: 48, width: 264, height: 112, paintOutset: 32 }]);
    cardAtSample = false;
    cards = [{ getBoundingClientRect: () => ({ x: 192, y: 200, left: 192, top: 200, right: 456, bottom: 312, width: 264, height: 112 }), contains: () => false }];
    frame();
    assert.equal(updates.at(-1).visible, true);
    assert.deepEqual(updates.at(-1).occlusions, [{ x: 192, y: 200, width: 264, height: 112, paintOutset: 32 }]);
    blocked = true;
    frame();
    assert.equal(updates.at(-1).visible, false);
    blocked = false;
    cards = [];
    frame();
    assert.equal(updates.at(-1).occlusions, undefined);
    document.hidden = true;
    documentEvents.dispatchEvent(new Event("visibilitychange"));
    assert.equal(updates.at(-1).visible, false);
    document.hidden = false;
    documentEvents.dispatchEvent(new Event("visibilitychange"));
    assert.equal(updates.at(-1).visible, true);
    stop();
    assert.equal(disconnected, true);
    const count = updates.length;
    windowEvents.dispatchEvent(new Event("scroll"));
    assert.equal(updates.length, count);
  } finally {
    for (const [key, value] of Object.entries(previous)) {
      if (value === undefined) delete globalThis[key];
      else globalThis[key] = value;
    }
  }
});
