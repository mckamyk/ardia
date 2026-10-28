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

## Still failing on 0f5d71dc: what Chromium 156 does at runtime

F15D on 0f5d71dc (which has the change above) still showed no entry. A run of the packaged `chrome` under gdb, with symbols from the matching unstripped build (same build ID), logged `WebContentsImpl::MediaStartedPlaying/MediaStoppedPlaying`, `LinuxPageMedia`'s observers, `LinuxMediaReporter::Send`, `LinuxPageState::EmitPresentation` and the renderer's `MediaPlayerObserver::OnMediaPlaying/OnMediaPaused`:

- The outside-session page loads in a background tab. Its three muted players start (`MediaStartedPlaying`, video 1, audio 0), and `LinuxPageMedia` emits `nowPlaying` with `hasVideo`, which reaches the page state.
- Blink's autoplay policy pauses muted autoplay videos that aren't visible (`third_party/blink/renderer/core/html/media/autoplay_policy.cc:382-384`, `PauseInternal(kAutoplayAutoPause)`). The renderer sends `OnMediaPaused` for each, and the browser reports `MediaStoppedPlaying`. The last stop withdrew the entry (`nowPlaying: null`).
- When the tab is shown, Blink resumes them: the page reports all three `paused: false`, `currentTime` 0.35 s, visible. The browser gets no `OnMediaPlaying`, so no `MediaStartedPlaying` follows. #283 counted only players the browser believed were playing, so the entry never came back.

macOS's page script keeps reporting a video that has played even while it is paused (`packages/ardiacore/ios/page_script.js:75-85`, `pickMedia` falls back to the current element), so its control survives a pause.

## Fix

- `LinuxPageVideos<Id>` (`ac_linux_media.h`) keeps the main-frame players that have started, with their `has_video` and whether the browser last saw them playing. Players leave only when they are destroyed (`MediaDestroyed`) or belong to a page that is no longer primary (`PrimaryPageChanged`). A stop only marks a player paused.
- `LinuxNowPlayingFromSession` reports a page video with no session as `playing` or `paused` (from the tracker) with `hasVideo: true` and `controllable: false`.
- A `LinuxPageMedia` created after the page's players exist, as for the native launch tab adopted by the shell or a tab rebound to another window, seeds the tracker from `ArdiaLinuxPictureInPictureCandidates` (main-frame players with video and PiP available, with their playing state).
- The PiP press is unchanged. The element route already takes the browser's playing flag first and then the largest video. With every player reported paused, `big` (480×270) wins over `small`; `skipped` has `disablepictureinpicture` and is not a candidate.

| Command | Result |
| --- | --- |
| `python3 -m unittest test_media_policy` | 11 passed. `page-videos` replays the 156 sequence (three starts, three stops, no restart) and `page-video` maps a paused page video; both fail to compile on `linux` |
| `bash scripts/linux/syntax-check-156.sh ac_linux_page_media ac_linux_media.cc ac_linux_host.cc` | 3 of 3 pass |
| `test_auto_pip`, `test_auto_pip_profiles`, `test_contract` | OK |

Needs the next build: F15D's outside-session check.
