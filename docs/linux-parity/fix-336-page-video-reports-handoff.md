# #336: the page video comes from the page script, as on macOS

Issue: [#336](https://github.com/mckamyk/ardia/issues/336), a follow-up from the #318 review (#270). Branch `fix3-media-336`.

## Cause

After #318, Linux decided whether a tab has a video (`nowPlaying` with `hasVideo`, which drives the Picture in Picture control) from the browser's player notifications: `LinuxPageVideos` kept every main-frame player that had started, and dropped it only on `MediaDestroyed` or `PrimaryPageChanged`. The browser can't tell what the page is doing with those players:

- Removing a `<video>` only pauses it (`html_media_element.cc:5065-5076`); `MediaDestroyed` waits for garbage collection. The tab kept offering Picture in Picture for a video that was gone, and a pinned tab stayed awake (`tabKeepAlive.ts:43`).
- `PrimaryPageChanged` dropped the players of a page entering the back/forward cache. When it came back its players resumed without a new start, so nothing was re-added.
- Blink resumes a background-paused muted video without `OnMediaPlaying`, so the entry read `paused` while the video played.

macOS has none of these because its page script reports `nowPlaying` itself (`page_script.js`, `pickMedia`): it only reports elements still in the document or explicitly tracked, re-reports when a removed element pauses (`watchRemoval`), and reads `paused` from the element.

## Fix (Linux native only)

The shared page script already runs on Linux in every http(s) and file frame (Ardia's isolated world, #117) and already posts `nowPlaying`; the channel refused it. Now:

- `nowPlaying` is an accepted page message from any frame of the primary page, active and live (`LinuxPageSenderAccepted`), under the same per-document budget. `NormalizeLinuxPageMessage` keeps only `{hasVideo, playing}`: `hasVideo` is true only for a boolean `true`, `playing` only for `playbackState: "playing"`. `null` clears.
- `LinuxPageChannel::NoteVideo` stores it on the sending document (`LinuxPageDocumentVideo`, a `DocumentUserData`) and notifies listeners. It is stored even before a tab binding attaches the channel, so a launch tab adopted later, or a tab rebound to another window, still has its report (this replaces the `ArdiaLinuxPlayedVideoPlayers` seed).
- `LinuxPageMedia::PageVideo()` joins the reports of the live documents of the primary page (main frame and iframes). It is recomputed on a report, `PrimaryPageChanged` (so a page restored from the back/forward cache brings its stored report back, and a page that left takes its report with it), `RenderFrameDeleted` (a removed iframe or a crashed renderer) and a cross-document commit.
- The browser-side tracker (`MediaStartedPlaying`/`MediaStoppedPlaying`/`MediaMetadataChanged`, `LinuxPageVideos`) is gone, and `chromium-linux-media-player-pip.patch` no longer adds `has_played_` or `ArdiaLinuxPlayedVideoPlayers`. The Picture in Picture candidates and request in that patch are unchanged.
- The Media Session side is unchanged: title, actions, artwork, controllable and tab protection still come from Chromium's session. Only `hasVideo` and, with no session, `playing`/`paused` come from the page.

The shared page script, the shared browser and macOS are unchanged.

## Behaviour now matching macOS

- A playing muted video, in the page or in an iframe, gives the tab a non-controllable `nowPlaying` with `hasVideo`.
- A removed playing video pauses, the page script re-reports, and the entry goes away.
- A page restored from the back/forward cache has its control again; its playing state is the element's.
- A frame whose page script picks a heard `<audio>` over a muted `<video>` reports no video, as on macOS. #270's extra case (an audible audio element beside a muted video in the same frame) no longer shows the control.
- A video already paused when it is removed sends no `pause`, so the script doesn't re-report and the entry stays until the page navigates. macOS has the same gap; fixing it means changing the shared page script.
- The script runs only in http(s) and file documents, as on macOS.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_page_messages` | 10 passed. `kinds`, `senders` and `normalization` cover `nowPlaying`: accepted from the main frame and iframes, refused from cached and prerendered documents, rebuilt to `{hasVideo, playing}`, `null` clears. Fails to compile on `linux` |
| `python3 -m unittest test_media_policy` | 11 passed. `page-videos` joins frame reports; fails to compile on `linux` |
| `python3 -m unittest test_page_videos` | 3 passed (new). The channel stores reports per document before a tab attaches; the tab entry follows the live documents of the primary page; no browser player tracking. 2 failures and 1 error on `linux` |
| `python3 -m unittest test_element_picture_in_picture` | 4 passed. The patch keeps no played-player list; fails on `linux` |
| `bash scripts/linux/syntax-check-156.sh ac_linux_page_media ac_linux_page_channel ac_linux_page_messages ac_linux_host.cc` | 4 of 4 pass |

## Needs the next Chromium build

F15D's outside-session check, plus by hand: remove a playing muted video from the DOM (the control goes away); go back to a video page restored from the back/forward cache (the control is there and reads playing).
