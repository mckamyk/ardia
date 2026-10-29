# Fix handoff: accepting a site's notification prompt on Linux (#358 follow-up)

JS only, in `apps/linux`. macOS, the shared browser and the demo are unchanged.

## Cause

Accepting a site's notification permission prompt calls `answerPermission` (`state/site/permissions.ts`), which drops the promise of `ensureNotificationPermission` (`state/site/webNotifications.ts`). That function awaits `notificationPermission()` with no catch. On Linux, `ArdiaApp.notificationPermission` rejects: `ERR_UNAVAILABLE` from `appModuleFallbacks.ts` (#358), or "Unsupported Linux host method" before it. So every accepted prompt left an unhandled rejection, which shows up as a `$exception` in telemetry. The #358 handoff wrongly said every caller of those methods catches. `showWebNotification` has the same pattern, but Linux never calls it: web notifications stay in Chromium and never reach the shell's `onNotification`.

## Fix

`notificationPermission.ts` replaces `webNotifications.ts` for `permissions.ts` only (an importer-scoped entry in `vite.config.ts`). It re-exports the shared module and wraps `ensureNotificationPermission` with `.catch(() => false)`. On Linux the desktop permission isn't Ardia's to ask, so the prompt settles quietly as "not granted". The site's own permission is still granted by Chromium.

## Tests

- `hostCapabilities.test.mjs`: with `notificationPermission` rejecting `ERR_UNAVAILABLE`, the Linux `ensureNotificationPermission` answers false and no unhandled rejection is recorded; the Vite entry is pinned.
- `scripts/module-graph.test.mjs` (`test:graph`): `permissions.ts` imports the Linux adapter, not the shared module, and the adapter imports the shared module.
