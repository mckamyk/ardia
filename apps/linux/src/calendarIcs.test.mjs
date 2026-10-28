process.env.TZ = "America/Los_Angeles";

import assert from "node:assert/strict";
import test from "node:test";
import { expandRule, ianaName, ianaZone, occurrences, parseIcs, parseRule, property, vtimezoneZone, events, unescapeText } from "./calendarIcs.ts";

const wall = (year, month, day, hour = 0, minute = 0, second = 0) => ({ year, month, day, hour, minute, second });
const day = (w) => `${w.year}-${String(w.month).padStart(2, "0")}-${String(w.day).padStart(2, "0")}`;
const expand = (start, rule, allDay = false) => expandRule(start, allDay, parseRule(rule), () => false, () => false).map(day);
const until = (start, rule, allDay = false) => {
  const parsed = parseRule(rule);
  return expandRule(start, allDay, parsed, (w) => w.year > 2030, (w) => day(w) > day(parsed.until.wall)).map(day);
};

test("monthly, yearly and weekly rules expand like RFC 5545", () => {
  assert.deepEqual(expand(wall(2026, 1, 30, 9), "FREQ=MONTHLY;BYDAY=-1FR;COUNT=4"), ["2026-01-30", "2026-02-27", "2026-03-27", "2026-04-24"]);
  assert.deepEqual(expand(wall(2026, 1, 31, 9), "FREQ=MONTHLY;BYMONTHDAY=31;COUNT=4"), ["2026-01-31", "2026-03-31", "2026-05-31", "2026-07-31"]);
  assert.deepEqual(expand(wall(2026, 1, 30, 9), "FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1;COUNT=4"), ["2026-01-30", "2026-02-27", "2026-03-31", "2026-04-30"]);
  assert.deepEqual(expand(wall(2024, 2, 29), "FREQ=YEARLY;COUNT=3", true), ["2024-02-29", "2028-02-29", "2032-02-29"]);
  assert.deepEqual(expand(wall(2026, 9, 1, 10), "FREQ=WEEKLY;INTERVAL=2;BYDAY=TU,TH;WKST=SU;COUNT=6"), ["2026-09-01", "2026-09-03", "2026-09-15", "2026-09-17", "2026-09-29", "2026-10-01"]);
  assert.deepEqual(expand(wall(2026, 3, 1), "FREQ=YEARLY;BYMONTH=3;BYDAY=2SU;COUNT=2", true), ["2026-03-01", "2026-03-08"]);
  assert.deepEqual(expand(wall(2026, 1, 1), "FREQ=YEARLY;BYYEARDAY=1,-1;COUNT=3", true), ["2026-01-01", "2026-12-31", "2027-01-01"]);
  assert.deepEqual(expand(wall(2026, 1, 5), "FREQ=YEARLY;BYWEEKNO=1;BYDAY=MO;COUNT=3", true), ["2026-01-05", "2027-01-04", "2028-01-03"]);
});

test("UNTIL, the hourly frequency and the DTSTART instance are honoured", () => {
  assert.deepEqual(until(wall(2026, 11, 3), "FREQ=DAILY;UNTIL=20261105", true), ["2026-11-03", "2026-11-04", "2026-11-05"]);
  const hourly = expandRule(wall(2026, 11, 3, 22), false, parseRule("FREQ=HOURLY;INTERVAL=3;COUNT=3"), () => false, () => false);
  assert.deepEqual(hourly.map((w) => `${day(w)}T${w.hour}`), ["2026-11-03T22", "2026-11-04T1", "2026-11-04T4"]);
  assert.deepEqual(expand(wall(2026, 11, 4, 9), "FREQ=WEEKLY;BYDAY=MO;COUNT=2"), ["2026-11-04", "2026-11-09"]);
  assert.equal(parseRule("FREQ=FORTNIGHTLY"), null);
});

const BERLIN = parseIcs([
  "BEGIN:VTIMEZONE", "TZID:Berlin",
  "BEGIN:STANDARD", "DTSTART:16011028T030000", "TZOFFSETFROM:+0200", "TZOFFSETTO:+0100", "RRULE:FREQ=YEARLY;BYDAY=-1SU;BYMONTH=10", "END:STANDARD",
  "BEGIN:DAYLIGHT", "DTSTART:16010325T020000", "TZOFFSETFROM:+0100", "TZOFFSETTO:+0200", "RRULE:FREQ=YEARLY;BYDAY=-1SU;BYMONTH=3", "END:DAYLIGHT",
  "END:VTIMEZONE",
].join("\r\n"))[0];

test("an embedded VTIMEZONE agrees with the system zone database", () => {
  const embedded = vtimezoneZone(BERLIN);
  const system = ianaZone("Europe/Berlin");
  for (let month = 1; month <= 12; month++) {
    for (const [d, h] of [[1, 12], [15, 0], [25, 1], [25, 4], [28, 23]]) {
      const w = wall(2026, month, d, h);
      assert.equal(embedded.toUtc(w), system.toUtc(w), JSON.stringify(w));
    }
  }
  assert.equal(new Date(embedded.toUtc(wall(2026, 10, 25, 1, 30))).toISOString(), "2026-10-24T23:30:00.000Z");
  assert.equal(new Date(embedded.toUtc(wall(2026, 10, 25, 3, 30))).toISOString(), "2026-10-25T02:30:00.000Z");
});

test("EDS and libical time zone ids resolve to system zones", () => {
  assert.equal(ianaName("/freedesktop.org/Tzfile/America/New_York"), "America/New_York");
  assert.equal(ianaName("/citadel.org/20190914_1/Europe/Berlin"), "Europe/Berlin");
  assert.equal(ianaName("Europe/Paris"), "Europe/Paris");
  assert.equal(ianaName("UTC"), "UTC");
  assert.equal(ianaName("W. Europe Standard Time"), null);
  assert.equal(ianaName("Custom/Nowhere"), null);
});

test("lines unfold, quoted parameters keep their colons and text unescapes", () => {
  const [event] = events(parseIcs("BEGIN:VEVENT\r\nUID:a\r\nORGANIZER;CN=\"Doe, Jane: PM\":mailto:jane@example.com\r\nSUMMARY:Quarterly plan\r\n ning\\, part 2\r\nEND:VEVENT\r\n"));
  assert.equal(property(event, "ORGANIZER").params.CN, "Doe, Jane: PM");
  assert.equal(property(event, "ORGANIZER").value, "mailto:jane@example.com");
  assert.equal(unescapeText(property(event, "SUMMARY").value), "Quarterly planning, part 2");
});

test("exceptions, extra dates and events that straddle the range edge", () => {
  const objects = events(parseIcs([
    "BEGIN:VEVENT", "UID:gym", "DTSTART;VALUE=DATE:20261102", "RRULE:FREQ=DAILY;COUNT=5", "EXDATE;VALUE=DATE:20261103", "RDATE;VALUE=DATE:20261110", "END:VEVENT",
    "BEGIN:VEVENT", "UID:overnight", "DTSTART:20261101T230000Z", "DTEND:20261102T090000Z", "END:VEVENT",
    "BEGIN:VEVENT", "UID:reminder", "DTSTART:20261102T080000Z", "END:VEVENT",
    "BEGIN:VEVENT", "UID:unknown-zone", "DTSTART;TZID=Mars/Olympus:20261102T120000", "DTEND;TZID=Mars/Olympus:20261102T130000", "END:VEVENT",
  ].join("\r\n")));
  const found = occurrences(objects, Date.UTC(2026, 10, 2, 8), Date.UTC(2026, 10, 12), () => null);
  const label = (o) => `${property(o.event, "UID").value}@${new Date(o.start).toISOString()}`;
  assert.deepEqual(found.map(label), [
    "overnight@2026-11-01T23:00:00.000Z",
    "gym@2026-11-02T08:00:00.000Z",
    "reminder@2026-11-02T08:00:00.000Z",
    "unknown-zone@2026-11-02T20:00:00.000Z",
    "gym@2026-11-04T08:00:00.000Z",
    "gym@2026-11-05T08:00:00.000Z",
    "gym@2026-11-06T08:00:00.000Z",
    "gym@2026-11-10T08:00:00.000Z",
  ]);
});

test("a long-running daily rule stays fast", () => {
  const objects = events(parseIcs("BEGIN:VEVENT\r\nUID:daily\r\nDTSTART;TZID=Europe/Berlin:20000101T080000\r\nRRULE:FREQ=DAILY\r\nEND:VEVENT\r\n"));
  const started = performance.now();
  const found = occurrences(objects, Date.UTC(2026, 10, 2), Date.UTC(2026, 10, 3, 12), (tzid) => ianaZone(tzid));
  assert.deepEqual(found.map((o) => new Date(o.start).toISOString()), ["2026-11-02T07:00:00.000Z", "2026-11-03T07:00:00.000Z"]);
  assert.ok(performance.now() - started < 2000);
});
