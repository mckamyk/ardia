# F14 handoff: Linux calendar backend (#24, source work #223)

Linux now reads calendars from Evolution Data Server (EDS) over the D-Bus session bus. EDS is what GNOME Online Accounts, GNOME Calendar and Evolution store calendars in. The shared calendar state, the pinned-calendar preview, the countdown badge and the meeting alert with Join all run on it unchanged. When EDS isn't there, Ardia says so instead of showing an empty day.

This was built and tested from source only. The native service passes the Chromium 156 syntax check, and its EDS client has run against a fake EDS on a real `dbus-daemon`. None of it has run inside a Chromium 156 build yet: the engine at `~/ardia-linux-build-156` predates this work.

## Pieces

| Layer | File | Role |
| --- | --- | --- |
| EDS client (plain C++) | `engine/ardiacore-linux/src/ardia/linux/ac_linux_calendar_eds.{h,cc}` | Loads GIO with `dlopen`, like the keyring importer. Opens its own private session-bus connection, finds the newest `org.gnome.evolution.dataserver.Sources<N>` and `Calendar<N>` names, lists sources, opens calendars, reads objects and time zones, and watches for changes. |
| App-wide service | `ac_linux_calendar_service.{h,cc}` | One `NoDestructor` service for the whole app. It owns a `base::Thread` with a GLib message pump, and every D-Bus call and every signal runs on that thread. It also stores consent, keeps the status, and debounces change notices (750 ms) before broadcasting them. |
| Host glue | `ac_linux_host.cc` (narrow) | Advertises the five `ArdiaCalendar.*` capabilities and routes them with the app profile's document folder. It subscribes each window on its first call and re-emits `onCalendarChanged`. |
| Deadlines | `bootstrap.js` | `requestAccess` and `objects` get 120 s and `sources` gets 60 s, because opening an online calendar can take a while. |
| Adapter | `apps/linux/src/hostCalendar.ts` | The `ArdiaCalendar` module for Linux (`authorizationStatus`, `requestAccess`, `calendars`, `events`), wired from `hostModules.ts` in place of the hard-coded `"denied"`. |
| iCalendar | `apps/linux/src/calendarIcs.ts` | Parses iCalendar, expands recurrences (RRULE, RDATE, EXDATE, RECURRENCE-ID overrides) and resolves time zones. |
| Wording | `apps/linux/calendarWording.ts` | A Vite transform that rewrites the shared Calendar pane and preview for Linux. A build fails if the shared copy changes underneath it. |
| Settings window | `apps/linux/src/utility.ts` | The Settings utility renderer starts the shared calendar store, so its Calendar pane shows the real state. |

## Native contract

Each `ArdiaCalendar` call takes positional arguments and replies with JSON:

| Call | Arguments | Reply |
| --- | --- | --- |
| `status` | none | `{status, available, message}` |
| `requestAccess` | none | `{granted, status, message}` |
| `sources` | none | `{sources: [{uid, calendar, data: {group: {key: value}}}]}` |
| `objects` | `[ids], start, end` | `{calendars: {uid: {status, message, objects: [ics]}}}` |
| `timezones` | `uid, [tzid]` | `{timezones: {tzid: ics or null}}` |

- **Status.** `status` is `notDetermined`, `fullAccess`, `restricted` (EDS isn't reachable) or `denied` (EDS or the bus refused).
- **`status` and `requestAccess`:**
  - `status` only probes the bus (`ListNames` and `ListActivatableNames` without auto-start), so it never starts EDS.
  - `requestAccess` saves `linux-calendar.json` (`{"version":1,"connected":true}`) in the app profile's document folder, then lists sources. Every window's bootstrap carries that document, so a new window knows the consent at once.
- **`sources`, `objects` and `timezones`:**
  - They refuse with `ERR_CALENDAR_NOT_CONNECTED` until the user connects.
  - `objects` and `timezones` only accept calendar sources from the last listing.
  - Limits: 64 calendars, a 400-day range, 20,000 objects and 16 MiB per calendar, 64 time zones per call.
- **Errors:**
  - `ERR_CALENDAR_UNAVAILABLE` means no bus, no EDS, or EDS stopped.
  - `ERR_CALENDAR_DENIED` covers `AccessDenied`, `AuthFailed`, `PermissionDenied`, `AuthenticationRequired` and similar refusals.
  - `ERR_CALENDAR_FAILED` covers everything else, such as timeouts or a backend that keeps closing.
  - `ERR_CALENDAR_REQUEST` means bad arguments.
  - Inside `objects`, each calendar carries its own status, so one expired account doesn't hide the others.
- **Event.** `ArdiaCalendar/onCalendarChanged` carries `{status, message, reason}`, where `reason` is `status`, `objects`, `sources` or `service`. It is broadcast when:
  - the status changes;
  - a call fails with a status;
  - a connect finishes;
  - a calendar view reports `Objects*`;
  - the source manager reports an added or removed source or a property change;
  - an EDS name loses its owner.

## Event mapping

| `SystemCalendarEvent` field | Comes from |
| --- | --- |
| `id` | `<calendar>/<UID>` |
| `occurrence` | `<calendar>/<UID>@<start seconds>`. macOS also keys occurrences by start seconds. |
| `title`, `location`, `notes` | `SUMMARY`, `LOCATION` and `DESCRIPTION`, with escapes undone |
| `url` | The first http(s) value of `URL`, `X-GOOGLE-CONFERENCE`, `X-MICROSOFT-SKYPETEAMSMEETINGURL` or `CONFERENCE`. The shared `joinLink` also scans location and notes. |
| `cancelled` | `STATUS:CANCELLED` |
| `declined` | The attendee whose address is the account's own has `PARTSTAT=DECLINED`. The account's addresses come from `[WebDAV Backend] EmailAddress`, `[Authentication] User`, `[Ews Backend] Email`, and the parent collection's `Identity`, `User` and mail address. |
| `organizer`, `attendees` | Rooms and resources (`CUTYPE`) are dropped. `PARTSTAT` maps to accepted, declined or tentative; anything else is pending. |
| `allDay` | `VALUE=DATE` events start and end at local midnight, as EventKit does. |

How times are resolved:
- **Floating times** use the viewer's zone.
- **TZIDs** resolve in order:
  1. an IANA name, including EDS and libical prefixes such as `/freedesktop.org/Tzfile/Europe/Berlin`;
  2. a VTIMEZONE embedded in the object, evaluated from its own STANDARD and DAYLIGHT rules;
  3. EDS `GetTimezone`, used for Outlook names like `W. Europe Standard Time`;
  4. otherwise, the viewer's zone.

How recurrences expand:
- **Engine.** Recurrences expand in the event's own zone with `dateutil` semantics, including BYSETPOS, negative BYMONTHDAY and BYDAY ordinals, BYWEEKNO and BYYEARDAY.
- **DTSTART** always counts as the first instance, as in libical. COUNT counts from DTSTART.
- **Old instances.** Instances that end before the range still count towards COUNT, but they aren't kept.
- **Limits.** One rule stops at 200,000 periods or 5,000 kept instances.

Calendar fields:
- **Account.** The parent source's display name. Without a parent, the backend's stub name (On This Computer, On The Web, Contacts, Weather).
- **`accountType`.** caldav and google map to `calDAV`, webcal to `subscribed`, contacts to `birthdays`, and ews or microsoft365 to `exchange`.
- **`owned`.** False for webcal, contacts and weather.
- **Color.** Normalized to `#RRGGBB`, falling back to macOS's grey.
- **Skipped sources.** Disabled sources, sources under a disabled collection, task lists and memo lists are left out.

## Limits and failure isolation (review round 1)

**Recurrence**
- Expansion jumps straight to the range for every rule without COUNT. It also jumps for a counted rule with a fixed step and no BY parts (SECONDLY to WEEKLY), working the count out arithmetically. Only counted rules with BY parts still walk from DTSTART.
- One rule stops at 50,000 periods. One refresh shares a 750 ms expansion budget. Running out of either puts the event in `lastRefresh().truncatedEvents` and logs a warning rather than silently dropping it.
- Years 0 to 99 no longer read as 19xx.

**Parsing**
- Components nested more than 16 deep are refused, and the tree walkers are iterative.
- A bad object is skipped and counted in `skippedObjects`. A calendar that throws is reported as failed without blanking the others.

**Native**
- Each `objects` request has one 90 s deadline, under the 120 s deadline on the JS side, and each calendar call is capped at whatever time is left.
- A calendar that times out is skipped for two minutes. Its entry is marked `timedOut`. The skip clears when an EDS service appears or vanishes.
- `status` answers from a separate thread-pool probe on its own short-lived connection, so it never waits behind a stuck calendar open.
- One `NameOwnerChanged` watch covers the EDS namespace. When `Sources<N>` or `Calendar<N>` gains an owner, the service re-probes and broadcasts, so `restricted` recovers by itself.
- An `objects` reply holds at most 32 MiB across all its calendars. String sizes are checked before copying, and capped calendars and replies carry `truncated`.
- Times are clamped to ±8.64e15 ms before the int64 cast. A query outside years 1 to 9999, or a failed `gmtime_r`, builds no query.

## Decisions

- **Consent before reading.** A Linux desktop has no privacy prompt for calendars, so Connect Calendar is the consent, as on macOS. Nothing beyond the bus probe happens before it. The consent is app-wide, matching macOS's app-wide calendar permission.
- **Disconnect.** It means the backend going away: EDS stops, an account or calendar is removed or disabled, or the bus is lost. Calendars then turn `restricted`, or the removed calendar disappears, and the events clear. The shared UI has no in-app Disconnect button on macOS either, and none was added.
- **ICS and CalDAV subscriptions.** The shared UI has no way to add one, so none was invented. A webcal calendar that GNOME Calendar subscribed to comes through EDS as `subscribed`.
- **Private connection.** The client uses its own `g_dbus_connection_new_for_address_sync` connection rather than GLib's shared one. The shared connection raises SIGTERM on the process when the bus closes; a fixture test proves the helper survives losing its bus.
- **Settings.** Calendar settings (`alertLead`, `alertCondition`, `showPreview`, `showTimeToNext`, `hiddenCalendarIds`) already sync and flush through the F04 Live document. This lane doesn't change that path. Hiding a calendar refreshes, and hidden calendars are never asked for objects.

## Tests

- **`engine/ardiacore-linux/tests/test_calendar.py`** (16 tests). It compiles the EDS client with ASan and UBSan into `tests/fixtures/calendar_eds_driver.cc`, then drives it under `dbus-run-session` against `tests/fixtures/fake_eds.py`. The bus tests cover:
  - unavailable with no EDS;
  - key-file sources;
  - one open per calendar, with the UTC range query and a silent view (`SetFlags 0` before `Start`);
  - denied for both refusal kinds;
  - view, source and backend-loss signals;
  - reopening a backend that vanished unnoticed;
  - Close and Dispose;
  - the newest interface version winning;
  - the probe not starting an activatable EDS while reading does;
  - surviving the loss of the session bus.

  The source tests cover the build registration, the one app-wide GLib-pumped thread, consent before any read, host routing through the app profile, and the method list and consent document matching between native and adapter.
- **`engine/ardiacore-linux/tests/bootstrap.test.mjs`.** The calendar request deadlines.
- **`apps/linux/src/hostCalendar.test.mjs`** (8 tests, `TZ=America/Los_Angeles`). States, connect, broadcast ordering, unavailability, source mapping, and a full fixture covering:
  - a weekly America/New_York series with an EXDATE and a moved instance;
  - an all-day event;
  - a declined event with a room;
  - a cancelled event in a `/freedesktop.org` zone;
  - a custom inline VTIMEZONE;
  - an Outlook zone fetched through `timezones`;
  - a floating event;
  - DST keeping local time;
  - filtering and partial failures.
- **`apps/linux/src/calendarIcs.test.mjs`** (7 tests). RFC rules, UNTIL, hourly rules, the embedded Berlin VTIMEZONE matching ICU across 2026, TZID mapping, unfolding and escapes, EXDATE, RDATE and range edges, and a 26-year daily rule in about 25 ms.
- **`apps/linux/src/calendarWording.test.mjs`.** The wording replacements and the build guard.
- **Acceptance `F14` (#24)** in `apps/linux/scripts/acceptance/scenarios/f14-calendar.mjs`.
  - The browser launches against a private `dbus-daemon` running the fake EDS. The checks: notDetermined with no reads; connecting writes the consent; the events are right; the Join alert picks the accepted meeting over the declined and cancelled ones; unchecking a calendar stops its reads; an EDS change appears with no refresh; stopping EDS turns the calendar `restricted` and clears it.
  - A second launch has no bus and a saved consent. The calendar is `restricted`, and Settings › Calendar shows the EDS wording and Try Again with no macOS text.
  - Its fixtures are self-tested in `tests/f14.test.mjs`. The scenario itself needs an engine built with this change.

## Open items

- **Runtime run.** Run acceptance `--only F14` on the next Chromium 156 build that includes these sources.
- **Desktop checks.** These need a real GNOME Online Accounts account and are listed as the scenario's manual steps:
  - a real account connecting;
  - an expired account re-authenticating.
- **RANGE=THISANDFUTURE overrides** are treated as single-instance overrides.
