# #291: muted videos inside iframes get the Picture in Picture control

Issue: [#291](https://github.com/mckamyk/ardia/issues/291), from the review of #283 (#270). Branch `fix3-media-291`, stacked on `fix3-media-336`.

## Cause

Two pieces were limited to the primary main frame:

- The tab's page-video entry (`nowPlaying` with `hasVideo`) counted only main-frame players (`ac_linux_page_media.cc`), so a muted video in an embedded player's iframe gave no control.
- The element route of the control's press (#124) listed and requested only main-frame players (`ArdiaLinuxPictureInPictureCandidates`, `ArdiaLinuxRequestPictureInPicture` in `chromium-linux-media-player-pip.patch`).

macOS's page script runs in every frame (`forMainFrameOnly: false`) and the shell keeps one `nowPlaying` per frame, so an iframe's video shows the control there.

## Fix

- The entry: #336 (`fix3-media-336`) takes it from the page script's `nowPlaying` of every live frame of the primary page, iframes included.
- The press, in `chromium-linux-media-player-pip.patch`:
  - `MediaWebContentsObserver::ArdiaLinuxInPrimaryPage` accepts a player whose frame is in this contents' primary page (main frame or iframe; not a fenced frame's page, not a back/forward-cached or prerendered page).
  - `ArdiaLinuxPictureInPictureCandidates()` lists the PiP-available video players of the whole primary page; the host still picks the playing, then largest one, and gives the page no user activation.
  - `ArdiaLinuxRequestPictureInPicture` checks the same predicate instead of comparing with the main frame.
  - The browser asks the player's renderer to enter PiP through the player's own `MediaPlayer` remote, so a cross-site iframe works the same way. Blink still applies the iframe's `picture-in-picture` permissions policy.
- Also fixed in the same patch: `ArdiaLinuxPictureInPictureAvailable` called `MediaSessionController::IsPictureInPictureAvailable(id.player_id)`, but the controller's `player_id_` is a session-wide counter, not the renderer's player id. Each call failed `CHECK_EQ(player_id_, player_id, base::NotFatalUntil::M160)` (seen in F15D's log on 148104a3: `Check failed: player_id_ == player_id (4 vs. 3)`), dumping without crashing, and would crash from M160. A Linux-only `ArdiaLinuxPictureInPictureAvailable()` accessor on the controller reads the flag directly.

macOS's Chromium element route only looks at the main frame, so there the iframe's control opens nothing; Linux opens the iframe's video.

## Tests

| Command | Result |
| --- | --- |
| `python3 -m unittest test_element_picture_in_picture` | 5 passed. Candidates and request use the primary-page predicate, the wrapper no longer compares with the main frame, and availability no longer passes `id.player_id`. 2 fail on `fix3-media-336` |
| clang `-fsyntax-only` of clean `media_web_contents_observer.cc`, `media_session_controllers_manager.cc` and `media_session_controller.cc` plus the patch, with `obj/content/browser/browser.ninja`'s flags | 3 of 3 pass |

## Needs the next Chromium build

A page with a muted, looping video in a cross-site iframe: the tab offers Picture in Picture, and pressing it opens the iframe's video without activating either document. F15D's log has no `media_session_controller.cc:192` check failures.

## Follow-up: which video the press opens

With candidates across every frame, "playing first, then largest" chose an autoplaying muted ad in a third-party iframe over the article's paused main-frame video. Each candidate now carries `muted` (`ArdiaLinuxPictureInPictureCandidate {id, playing, muted}`; the patch adds a Linux `PlayerInfo::muted()`). `ChooseLinuxPictureInPicturePlayer` ranks unmuted first, then main frame, then playing, then size. This is close to macOS's heard, playing, main-frame order.

`ArdiaLinuxRequestPictureInPicture` now checks the same eligibility as the candidate list (`ArdiaLinuxPictureInPictureEligible`: has video, a bound remote, PiP available, in the primary page) instead of relying on the caller.

Tests: `test_media_policy` `pip-choice` (muted iframe ad vs paused main-frame video, unmuted iframe over muted main frame, main frame over iframe, playing breaks ties), `test_element_picture_in_picture` 5. clang `-fsyntax-only` of the patched `media_web_contents_observer.cc` and `media_session_controllers_manager.cc` with `browser.ninja`'s flags passes.
