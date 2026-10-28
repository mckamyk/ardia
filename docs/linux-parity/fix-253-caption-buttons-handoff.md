# Fix handoff: Chrome's caption buttons under the pinned extension button (#253)

Native only: `ac_linux_host.cc` and a new test. No UI, shared or macOS code changed.

## Cause

Ardia windows are Chrome `BrowserView`s with the Ardia shell laid over the client view. On Linux the frame is `BrowserFrameViewLinux`, an `OpaqueBrowserFrameView`, and it still creates Chrome's minimize, maximize, restore and close buttons in the top-right corner. Since 156 the client view is a child of the frame view and `FrameView::GetChildrenInZOrder` moves it to the bottom, so the caption buttons (each painted to its own layer) sit above the shell: they draw the faint grey × and Views targeting gives them the press before the shell sees it. `OpaqueBrowserFrameView::NonClientHitTest` also reports `HTCLOSE`/`HTMAXBUTTON` there. The pinned extension button is under that corner, so clicking it closed the window on sway and maximized it on X11 (the corner holds a different button there).

## Fix

`ShellHostView::HideCaptionButtons()` calls `BrowserFrameView::SetCaptionButtonsOpacity(0.0f)` on the window's frame (through the public `BrowserWidget::GetFrameView()`). In `OpaqueBrowserFrameView` that sets each button's layer opacity to 0 and `SetCanProcessEventsWithinSubtree(false)`, and `HitTestCaptionButton` requires that flag, so the buttons neither paint nor take hits: the corner hit-tests like the rest of Ardia's top strip and the press reaches the shell. It runs from `LayoutOverlay()` (initialize and every BrowserView bounds change: resize, maximize, fullscreen) and from `OnWidgetThemeChanged`, which `NonClientView::UpdateFrame` triggers when the frame is recreated (`kThemeChangeOptimization` is off by default in 156, so observers are always notified). In 156 only Helium's zen mode changes the opacity afterwards; it follows a Helium layout pref that Ardia leaves at its default (off), and no accelerator toggles it. Ardia draws its own window controls, so nothing is lost.

**Known gap: accessibility.** The buttons stay visible Views, so they remain in the accessibility tree: a screen reader lists Chrome's Minimize, Maximize and Close next to Ardia's own controls, and the accessibility API could still press them. Tracked in #327 (likely `GetViewAccessibility().SetIsIgnored(true)` beside the opacity); not fixed here.

`--hide-window-buttons` (an ungoogled flag read by `OpaqueBrowserFrameViewLayout::LayoutWindowControls`) would also hide them, but it is process-wide: Chrome's own windows that Ardia does not host (undocked DevTools, `TYPE_POPUP` windows) would lose their close button.

## Tests

`engine/ardiacore-linux/tests/test_caption_buttons.py` (6 tests):
- the real `LayoutOverlay`, `HideCaptionButtons` and `OnWidgetThemeChanged`, compiled against fakes, hide the buttons on every layout and theme change and tolerate a missing view, widget or frame (failed before the fix: the functions didn't exist);
- against the 156 tree: Linux frames are `OpaqueBrowserFrameView`s whose buttons stack above the client view and answer `HTCLOSE`; opacity 0 clears `CanProcessEventsWithinSubtree` for all four buttons and both hit testing and Views targeting honour it; `BrowserWidget::GetFrameView` is public; only zen mode restores the opacity.

`scripts/linux/syntax-check-156.sh ac_linux_host.cc` passes.

## Needs the next build

Not compiled into a package. On the next Chromium 156 build run F09P (its last check clicks the pinned toolbar button) on sway and Xvfb + openbox: the popup opens, the window neither closes nor maximizes, and no × shows in the corner.
