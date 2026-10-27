import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src'
LINUX = SOURCE / 'ardia' / 'linux'
HOST = (LINUX / 'ac_linux_host.cc').read_text()

LEDGER = r'''#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "ardia/linux/ac_linux_window_lifecycle.h"

using namespace ardia::linux_window;

static void NewRegularWindowsHonorRequestedFocus() {
  FocusLedger ledger(true);
  assert(ledger.Create(7, {.focus = true}) == Presentation::kActivate);
  assert(ledger.pending(7));
  assert(ledger.Create(8, {.focus = false}) == Presentation::kInactive);
  assert(!ledger.pending(8));
  assert(ledger.Create(9, {.focus = true, .behind = true}) == Presentation::kInactive);
  assert(ledger.Create(10, {.focus = false, .utility = true}) == Presentation::kActivate);
}

static void BackgroundInstancesNeverActivate() {
  FocusLedger ledger(false);
  assert(ledger.Create(1, {.focus = true}) == Presentation::kInactive);
  assert(ledger.Create(2, {.utility = true}) == Presentation::kInactive);
  assert(ledger.Reuse(1, {.focus = true}, false) == Activation::kNone);
  assert(ledger.Focus(1) == Activation::kNone);
  assert(ledger.Ready(1) == Activation::kNone);
  assert(!ledger.pending(1));
}

static void ReuseFollowsIntentButIgnoresTheWindowsOwnLaunchEcho() {
  FocusLedger ledger(true);
  ledger.Create(1, {.focus = false});
  assert(ledger.Reuse(1, {.focus = true}, true) == Activation::kNone);
  assert(!ledger.pending(1));
  assert(ledger.Reuse(1, {.focus = false}, false) == Activation::kNone);
  assert(ledger.Reuse(1, {.focus = true, .behind = true}, false) == Activation::kNone);
  assert(ledger.Reuse(1, {.focus = true}, false) == Activation::kActivate);
  assert(ledger.pending(1));
  assert(ledger.Reuse(2, {.focus = false, .utility = true}, true) == Activation::kActivate);
}

static void ReadinessRetriesOnlyWhileNothingElseWasActivated() {
  FocusLedger ledger(true);
  ledger.Observe(1, true);
  ledger.Create(2, {.focus = true});
  assert(ledger.Ready(2) == Activation::kActivate);
  assert(!ledger.pending(2));
  assert(ledger.Ready(2) == Activation::kNone);

  ledger.Create(3, {.focus = true});
  ledger.Observe(1, false);
  assert(ledger.Ready(3) == Activation::kNone);

  ledger.Create(4, {.focus = true});
  assert(ledger.Observe(4, true));
  assert(!ledger.pending(4));
  assert(ledger.Ready(4) == Activation::kNone);

  ledger.Create(5, {.focus = true});
  ledger.Forget(5);
  assert(!ledger.pending(5));
  assert(ledger.Ready(5) == Activation::kNone);
}

static void ObservedActivationIsReportedOncePerTransition() {
  FocusLedger ledger(false);
  assert(ledger.Observe(1, true));
  assert(!ledger.Observe(1, true));
  assert(ledger.active() == 1);
  assert(ledger.Observe(2, true));
  assert(!ledger.Observe(1, false));
  assert(ledger.active() == 2);
  assert(!ledger.Observe(2, false));
  assert(ledger.active() == 0);
  assert(ledger.Observe(2, true));
  ledger.Forget(2);
  assert(ledger.active() == 0);
}

static void SharedFramesUseBottomUpPrimaryCoordinates() {
  const Rect primary{0, 0, 1920, 1080};
  const auto shared = SharedFrame({100, 50, 800, 600}, primary);
  assert((shared == std::array<int, 4>{100, 430, 800, 600}));
  const auto back = RequestedFrame({100, 430, 800, 600}, primary, true);
  assert(back && back->positioned);
  assert(back->bounds.x == 100 && back->bounds.y == 50);
  assert(back->bounds.width == 800 && back->bounds.height == 600);
  const Rect offset{-1280, 200, 1920, 1080};
  const auto moved = SharedFrame({-1280, 200, 640, 480}, offset);
  assert((moved == std::array<int, 4>{0, 600, 640, 480}));
  const auto restored = RequestedFrame({0, 600, 640, 480}, offset, true);
  assert(restored->bounds.x == -1280 && restored->bounds.y == 200);
  const auto rounded = RequestedFrame({10.4, 20.6, 800.5, 600.2}, primary, true);
  assert(rounded->bounds.width == 801 && rounded->bounds.height == 600);
}

static void WaylandFramesCarrySizeOnly() {
  const Rect primary{0, 0, 1920, 1080};
  const auto wayland = RequestedFrame({400, 300, 900, 700}, primary, false);
  assert(wayland && !wayland->positioned);
  assert(wayland->bounds.width == 900 && wayland->bounds.height == 700);
  GeometryLedger geometry(false);
  assert(!geometry.Frame({0, 0, 900, 700}, primary, false, false));
}

static void MalformedFramesAreRejected() {
  const Rect primary{0, 0, 1920, 1080};
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  assert(!RequestedFrame({0, 0, 800}, primary, true));
  assert(!RequestedFrame({0, 0, 0, 600}, primary, true));
  assert(!RequestedFrame({0, 0, 800, -1}, primary, true));
  assert(!RequestedFrame({nan, 0, 800, 600}, primary, true));
  assert(!RequestedFrame({0, inf, 800, 600}, primary, true));
  assert(!RequestedFrame({0, 0, 1e9, 600}, primary, true));
  assert(!RequestedFrame({1e12, 0, 800, 600}, primary, true));
}

static void FrameReportsSkipFullscreenMinimizedAndRepeats() {
  const Rect primary{0, 0, 1920, 1080};
  GeometryLedger geometry(true);
  const auto first = geometry.Frame({100, 50, 800, 600}, primary, false, false);
  assert(first && (*first == std::array<int, 4>{100, 430, 800, 600}));
  assert(!geometry.Frame({100, 50, 800, 600}, primary, false, false));
  assert(!geometry.Frame({0, 0, 1920, 1080}, primary, true, false));
  assert(!geometry.Frame({0, 0, 160, 28}, primary, false, true));
  assert(!geometry.Frame({0, 0, 0, 0}, primary, false, false));
  assert(geometry.Frame({120, 50, 800, 600}, primary, false, false));
  geometry.Reset();
  assert(geometry.Frame({120, 50, 800, 600}, primary, false, false));
}

static void FullScreenChangesAreReportedOnTransitionsOnly() {
  GeometryLedger geometry(false);
  assert(!geometry.FullScreen(false));
  assert(geometry.FullScreen(true) == std::optional<bool>(true));
  assert(!geometry.FullScreen(true));
  assert(geometry.FullScreen(false) == std::optional<bool>(false));
}

int main() {
  NewRegularWindowsHonorRequestedFocus();
  BackgroundInstancesNeverActivate();
  ReuseFollowsIntentButIgnoresTheWindowsOwnLaunchEcho();
  ReadinessRetriesOnlyWhileNothingElseWasActivated();
  ObservedActivationIsReportedOncePerTransition();
  SharedFramesUseBottomUpPrimaryCoordinates();
  WaylandFramesCarrySizeOnly();
  MalformedFramesAreRejected();
  FrameReportsSkipFullscreenMinimizedAndRepeats();
  FullScreenChangesAreReportedOnTransitionsOnly();
}
'''


def method(name):
    start = HOST.find(name)
    assert start >= 0, name
    end = HOST.find('\n}\n', start)
    return HOST[start:end]


class WindowLifecycleLedgerTests(unittest.TestCase):
    def test_focus_and_geometry_ledgers(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        with tempfile.TemporaryDirectory(prefix='ardia-window-lifecycle-') as scratch:
            scratch = pathlib.Path(scratch)
            cpp = scratch / 'lifecycle.cc'
            cpp.write_text(LEDGER)
            binary = scratch / 'lifecycle'
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Werror', '-I', str(SOURCE), str(cpp),
                            str(LINUX / 'ac_linux_window_lifecycle.cc'), '-o', str(binary)],
                           check=True, capture_output=True)
            subprocess.run([str(binary)], check=True)


class WindowLifecycleHostTests(unittest.TestCase):
    def test_regular_windows_no_longer_always_show_inactive(self):
        open_window = method('if (method == "ArdiaShell.openWindow")')
        self.assertNotIn('else created->GetWindow()->ShowInactive();', open_window)
        self.assertIn('WindowFocus().Create(', open_window)
        self.assertIn('WindowFocus().Reuse(', open_window)
        self.assertIn('options->FindBool("focus")', open_window)
        self.assertIn('options->FindString("behind")', open_window)

    def test_open_window_frames_use_the_shared_coordinate_contract(self):
        open_window = method('if (method == "ArdiaShell.openWindow")')
        self.assertIn('RequestedFrame(', open_window)
        self.assertNotIn('(*frame)[0].GetInt()', open_window)
        frame = method('if (method == "ArdiaShell.windowFrame")')
        self.assertIn('SharedFrameValue(', frame)

    def test_explicit_focus_goes_through_the_ledger(self):
        self.assertRegex(HOST, r'method == "ArdiaShell\.focusWindow"\) \{\n\s+ActivateNativeWindow\(target->browser\(\), WindowFocus\(\)\.Focus\(')

    def test_widget_observer_reports_activation_frame_and_fullscreen(self):
        for name in ('void OnWidgetActivationChanged(views::Widget* widget, bool active) override;',
                     'void OnWidgetBoundsChanged(views::Widget* widget, const gfx::Rect& bounds) override;',
                     'void OnWidgetShowStateChanged(views::Widget* widget) override;'):
            self.assertIn(name, HOST)
        activation = method('void ShellHostView::OnWidgetActivationChanged(')
        self.assertIn('WindowFocus().Observe(id_, active)', activation)
        self.assertIn('BroadcastWindowEvent(', activation)
        broadcast = method('void BroadcastWindowEvent(')
        self.assertIn('is_utility()', broadcast)
        self.assertIn('"onWindowEvent"', broadcast)
        self.assertIn('"fullScreen"', method('void ShellHostView::ReportWindowGeometry('))
        self.assertIn('"frame"', method('void ShellHostView::ReportWindowGeometry('))

    def test_utility_windows_do_not_publish_window_events(self):
        geometry = method('void ShellHostView::ReportWindowGeometry(')
        self.assertIn('if (is_utility()', geometry)
        activation = method('void ShellHostView::OnWidgetActivationChanged(')
        self.assertIn('is_utility()', activation)

    def test_renderer_readiness_retries_requested_activation(self):
        open_window = method('if (method == "ArdiaShell.openWindow")')
        self.assertRegex(open_window, r'if \(host == this\) host->RendererReady\(\);')
        self.assertIn('target->RendererReady();', method('if (method == "ArdiaShell.utilityReady")'))
        ready = method('void ShellHostView::RendererReady(')
        self.assertIn('WindowFocus().Ready(id_)', ready)
        self.assertIn('WindowState()', ready)

    def test_destroyed_windows_leave_the_ledger(self):
        self.assertIn('WindowFocus().Forget(id_);', method('ShellHostView::~ShellHostView('))
        self.assertIn('WindowFocus().Forget(browser->GetSessionID().id());', method('void OnBrowserClosed('))
        self.assertIn('if (is_utility() || window_id_.empty() || !widget || !browser_) return;', method('void ShellHostView::ReportWindowGeometry('))

    def test_page_focus_reports_split_pane_activation(self):
        self.assertIn('void OnWebContentsFocused(content::RenderWidgetHost*) override;', HOST)
        focused = method('void TabBinding::OnWebContentsFocused(')
        self.assertIn('"pageFocus"', focused)
        self.assertIn('presented', focused)

    def test_lifecycle_helper_is_built(self):
        build = (LINUX / 'BUILD.gn').read_text()
        self.assertIn('"ac_linux_window_lifecycle.cc"', build)
        self.assertIn('"ac_linux_window_lifecycle.h"', build)
        self.assertIn('"//ui/display"', build)
        self.assertIn('"//ui/ozone"', build)

    def test_new_sources_have_no_comments(self):
        for path in (LINUX / 'ac_linux_window_lifecycle.h', LINUX / 'ac_linux_window_lifecycle.cc'):
            text = path.read_text()
            self.assertIsNone(re.search(r'//|/\*', text), path)


if __name__ == '__main__':
    unittest.main()
