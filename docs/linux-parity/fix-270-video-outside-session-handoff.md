# #270: a playing video outside the media session gets the Picture in Picture control

Issue: [#270](https://github.com/mckamyk/ardia/issues/270), found by the desktop-session harness (#240, wave 2 pages lane, F15D). Branch `fix2-media-pip`.

## Cause

Site Controls and the tab's hover card show Picture in Picture when `useMedia().sessions[tab]?.hasVideo || pipOpen[tab]` (`SiteControls.tsx`, `HoverCard.tsx`, shared). On macOS the page script reports `nowPlaying` for any playing `<video>`, heard or not, with `controllable: false` when it was never heard, so a muted autoplaying video has a session entry with `hasVideo: true` and the control is there.

On Linux `nowPlaying` comes only from Chromium's Media Session (`LinuxPageMedia`). A muted or silent video never joins the session, so the tab had no entry, no control, and the element-level PiP route from #124 (`ArdiaLinuxRequestPictureInPicture`) could not be reached.

## Fix (Linux native only)

- `LinuxPageMedia` observes `MediaStartedPlaying`, `MediaStoppedPlaying`, `MediaMetadataChanged` and `MediaDestroyed` and keeps the playing players of the primary main frame with their `has_video`. Subframe players are ignored, as `ArdiaLinuxPictureInPictureCandidates` lists only main-frame players. The page title is re-read on `TitleWasSet` while a video plays.
- `LinuxNowPlayingFromSession` (`ac_linux_media.cc`):
  - no active session and a playing main-frame video: a `nowPlaying` with the page title, `playing`, `hasVideo: true`, no actions and `controllable: false`, the shape macOS reports for an unheard video;
  - an active session without video (for example an audible `<audio>` beside a muted video): `hasVideo` is also true while a main-frame video plays.
- `controllable: false` keeps the entry out of the player tile and the media keys (`playerTabFor`). Tab protection (`SessionPlaying`) and the PiP route stay session-based, so the tile's press still takes the element route (#124): the playing, then largest, PiP-available video, with no user activation given to the page.

The shared browser and macOS are unchanged.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_media_policy` | 10 passed; the new `page-video` case fails on `linux` (the sample has no page-video fields) |
| `node --test src/hostMedia.test.mjs` (in `test:host`) | 7 passed; a non-controllable video `nowPlaying` sets `hasVideo` for the controls and gives no player tile |
| `bash scripts/linux/syntax-check-156.sh ac_linux_page_media ac_linux_media.cc` | 2 of 2 pass |

## Needs the next build to confirm

F15D's "a playing muted video outside the media session gets a Picture in Picture control" check, against a package built from this branch.
