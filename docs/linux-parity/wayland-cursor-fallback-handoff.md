# Wayland cursor fallback (#273)

At launch a sidebar tab's hover card opened over the page's top-left corner with no pointer anywhere near it. The occlusion cut-out around it showed the window background, and clicks there hit the card instead of the page.

## Cause

The pointer event was synthesized by Chromium, not by a pointer. A recorder in the shell captured one trusted `pointerover`/`mouseover`/`pointermove` with zero movement about a second after load, at client (106, 100) and screen (116, 110). The private sway session had no pointer device at all. When sway re-tiled the window for Settings, a second event arrived at (100, 100) with movement −6.

Aura synthesizes mouse moves at `display::Screen::GetCursorScreenPoint()` when windows show or change bounds. On Wayland, with no window holding pointer focus, `WaylandScreen::GetCursorScreenPoint()` returns a point meant to lie outside every window: the bounds of `GetWindowWithLargestBounds()` plus 10. That helper compares windows with `gfx::Rect::operator<`, which orders by origin before size. Any popup or helper window off the origin therefore beats the browser window at (0, 0). Here a 96×90 window won, which put the "outside" point at (106, 100), inside the browser and over its selected tab row.

The same point reaches page renderers, so pages see a phantom hover there too.

## Fix

`engine/patches/chromium-linux-wayland-cursor-fallback.patch` (Linux only) returns a point beyond the union of every window's bounds (right + 10, bottom + 10). The comment above the code already states this intent. Positions while a window has pointer or touch focus are unchanged.

A UI-side guard was tried and dropped:
- **What it did.** It held pointer events until one moved.
- **Why it went.** CDP-injected moves (`revealToolbar` in F23) also carry no movement, so it would have swallowed them. It also missed the re-tile event, which moves by −6.

## Verification

- **Unit test.** `engine/ardiacore-linux/tests/test_wayland_cursor_fallback.py` compiles the real `GetCursorScreenPoint` body from the 156 tree, unpatched and patched, against small stubs:
  - The unpatched body puts the point at (106, 100) inside the browser when a 96×90 popup sits at (210, 25).
  - The patched body puts it at (1930, 1090), outside every window.
- **Patch.** It applies to the 156 tree without fuzz.
- **Compile check.** The patched file passes `clang -fsyntax-only` with the flags in `obj/ui/ozone/platform/wayland/wayland.ninja`.
- **Still needs.** The next Chromium build and an acceptance run: a launch with no pointer over the window must show no hover card, and clicks over the page's top-left must reach the page.
