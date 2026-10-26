import assert from "node:assert/strict";
import test from "node:test";
import { docs } from "../../test-native/index.ts";
import { updateCalendarSettings, live, setLive } from "./store.ts";

test("default persistence keeps its 800ms debounce and versioned projection", (context) => {
  context.mock.timers.enable({ apis: ["setTimeout"] });
  docs.delete("live.json");
  setLive({ status: { f: { state: "updating" } }, completing: { f: ["i"] }, showAll: { f: true } });
  updateCalendarSettings({ showPreview: false });
  assert.equal(docs.has("live.json"), false);
  context.mock.timers.tick(799);
  assert.equal(docs.has("live.json"), false);
  updateCalendarSettings({ showTimeToNext: false });
  context.mock.timers.tick(799);
  assert.equal(docs.has("live.json"), false);
  context.mock.timers.tick(1);
  const saved = JSON.parse(docs.get("live.json"));
  assert.equal(saved.version, 1);
  assert.equal(saved.calendar.showPreview, false);
  assert.equal(saved.calendar.showTimeToNext, false);
  for (const key of ["status", "completing", "showAll"]) assert.equal(key in saved, false);
  assert.equal(live().showAll.f, true);
});
