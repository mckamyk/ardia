# Fix handoff: extension popup focus and the page under it (#264, #260)

Linux UI only (`apps/linux`), plus two `testID`s on the shared popup overlay. No native change; verified end to end on the `0f5d71dc` package with this branch's UI.

## #264: the popup opened without keyboard focus

**Cause.** `ActionPopup` asks its view for focus when the popup page finishes loading. On Linux that is `extensionView.command focus`, and the native surface ignores it while its view is hidden (`Surface::Focus` returns when `!view_->GetVisible()`). At that moment the popup is still hidden: it fades in from opacity 0 and `watchTabViewport` reports a view under a transparent ancestor as not visible, so the first visible viewport comes later and nobody asks for focus again.

**Fix.** `HostExtensionView` remembers a focus request made while its last viewport was hidden and sends it right after the first visible viewport, in the same queue, so it reaches the native view once it is shown. A request while visible goes straight through. This matches #246's rule: a view inside the overlay holding focus is left alone on reactivation.

**Guard (review follow-up).** A deferred request only stands while nothing else has moved focus. It records the shell's `document.activeElement` and `document.hasFocus()` when it is made (a page or another surface taking focus clears `hasFocus`), plus the time. When the popup first shows, the focus is sent only if both are unchanged and no more than `DEFERRED_FOCUS_MS` (1.5 s) has passed; otherwise the request is dropped. A popup that loaded while hidden (still fading in, or behind an open menu while `menuShown()` hides it) so never takes focus the user already moved elsewhere in the window, in keeping with #246. The 1.5 s covers the fade-in even through the ~600 ms frame stalls seen under headless sway (#279).

## #260: the page went blank under the popup

**Cause.** A native page viewport is shown only when five sample points inside it hit its placeholder in the shell (`watchTabViewport`). The popup's full-window outside-click layer covered all five, so the page was hidden for as long as the popup was open.

**Fix.**
- The popup's dismiss layer (`testID="ardia-extension-popup-dismiss"`) counts as transparent, and the popup itself (`testID="ardia-extension-popup"`) is an occlusion like the sidebar hover cards: the page stays painted and live, cut out under the popup with 32 px of paint outset for its shadow (the existing captured backdrop fills that margin).
- The page now takes clicks outside the popup, as Chrome's does. A page that takes focus closes its window's popup (`popupDismissal.ts`, wired from `main.tsx`), which is how Chrome's popup closes on an outside click; the press still reaches the page. Clicks on Ardia's own UI still land on the dismiss layer. The popup's own extension view never treats itself as an occluder.

## Tests

- `hostExtensions.test.mjs`: focus asked before the popup is shown is sent once, right after the first visible viewport; while shown it goes straight through; hidden again, it waits again (failed before). A page focus in the popup's window closes it, one in another window doesn't, and the page's own handler still runs (failed before).
- `hostViewport.test.mjs`: with a popup open, the page under it stays visible with the popup as its only occlusion; any other element covering the page still hides it; the popup's own view isn't occluded by itself (failed before). The existing viewport tests' fake documents gained `querySelector`.
- `pnpm --filter @ardia/linux test:host` 537/537, `typecheck`, `test:graph`, `acceptance:self-test`.
- Guard: a deferred focus is dropped when the shell's active element or document focus changed, or after 1.5 s, and still sent at 1.5 s with nothing changed (failed before the guard). `test:host` 563/563, `typecheck`; F09P's focus check still passes end to end on the `0f5d71dc` package with this UI.

**End to end** (headless sway, `0f5d71dc` package, this branch's UI through the suite's UI-dir option): F09P's focus, outside-click, placement, Escape and Close checks pass (the focus check failed on the stock UI). A probe scenario on the same fixture: with the popup open the page's heading is on screen; a page click closes the popup and the page gets the `mousedown`; a sidebar click closes it; with the page focused first, the shortcut's popup still closes on a page click. On the stock UI the page click never reached the page.
