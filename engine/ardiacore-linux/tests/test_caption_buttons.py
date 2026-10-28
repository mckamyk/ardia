import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

from test_renderer_contract import method as extract

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOST = (ROOT / 'src/ardia/linux/ac_linux_host.cc').read_text()
TREE = pathlib.Path.home() / 'ardia-linux-build-156/chromium_git/chromium/src'
FRAME = TREE / 'chrome/browser/ui/views/frame'

FAKES = r'''
#include <cstdio>
#include <vector>
namespace gfx { struct Rect {}; }
namespace views {
class Widget {};
class View {
 public:
  virtual ~View() = default;
  void SetBoundsRect(const gfx::Rect&) {}
  gfx::Rect GetLocalBounds() const { return {}; }
  void SetVisible(bool visible) { visible_ = visible; }
  bool visible_ = true;
};
}
struct BrowserFrameView {
  std::vector<float> opacities;
  void SetCaptionButtonsOpacity(float opacity) { opacities.push_back(opacity); }
};
struct BrowserWidget {
  BrowserFrameView* frame = nullptr;
  BrowserFrameView* GetFrameView() const { return frame; }
};
struct BrowserView : views::View {
  BrowserWidget* widget = nullptr;
  views::View contents;
  BrowserWidget* browser_widget() const { return widget; }
  views::View* GetActiveContentsWebView() { return &contents; }
};
struct ShellHostView : views::View {
  BrowserView* browser_view_ = nullptr;
  views::View shell;
  views::View* shell_ = &shell;
  void LayoutOverlay();
  void HideCaptionButtons();
  void OnWidgetThemeChanged(views::Widget*);
};
'''

SCENARIO = r'''
#define EXPECT(condition) do { if (!(condition)) { std::printf("failed: %s\n", #condition); return 1; } } while (0)
int main() {
  ShellHostView detached;
  detached.LayoutOverlay();
  detached.OnWidgetThemeChanged(nullptr);
  BrowserView browser;
  ShellHostView host;
  host.browser_view_ = &browser;
  host.LayoutOverlay();
  EXPECT(!browser.contents.visible_);
  BrowserWidget widget;
  browser.widget = &widget;
  host.LayoutOverlay();
  BrowserFrameView frame;
  widget.frame = &frame;
  host.LayoutOverlay();
  EXPECT(frame.opacities == std::vector<float>{0.0f});
  host.LayoutOverlay();
  host.OnWidgetThemeChanged(nullptr);
  EXPECT((frame.opacities == std::vector<float>{0.0f, 0.0f, 0.0f}));
  return 0;
}
'''


def body(source, signature):
    return extract(source, signature)


class CaptionButtonHostTests(unittest.TestCase):
    def test_every_overlay_layout_and_theme_change_hides_chromes_caption_buttons(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        members = '\n'.join(body(HOST, signature) for signature in [
            'void ShellHostView::LayoutOverlay(',
            'void ShellHostView::HideCaptionButtons(',
            'void ShellHostView::OnWidgetThemeChanged('])
        with tempfile.TemporaryDirectory(prefix='ardia-caption-buttons-') as scratch:
            root = pathlib.Path(scratch)
            (root / 'host.cc').write_text(FAKES + members + SCENARIO)
            built = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                                    str(root / 'host.cc'), '-o', str(root / 'host')], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(root / 'host')], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_the_host_observes_theme_changes_and_includes_the_frame_headers(self):
        declaration = HOST[HOST.index('class ShellHostView : public views::View'):HOST.index('void LayoutOverlay();')]
        self.assertIn('void OnWidgetThemeChanged(views::Widget* widget) override;', declaration)
        self.assertIn('#include "chrome/browser/ui/views/frame/browser_frame_view.h"', HOST)
        self.assertIn('#include "chrome/browser/ui/views/frame/browser_widget.h"', HOST)


class CaptionButtonChromiumTests(unittest.TestCase):
    def setUp(self):
        if not (FRAME / 'opaque_browser_frame_view.cc').is_file():
            self.skipTest('Chromium 156 source is unavailable')

    def test_linux_frames_put_their_caption_buttons_above_the_client_view(self):
        frame_view = (TREE / 'ui/views/window/frame_view.cc').read_text()
        order = extract(frame_view, 'View::Views FrameView::GetChildrenInZOrder()')
        self.assertIn('paint_order.insert(paint_order.begin(), client_view)', order)
        factory = (FRAME / 'browser_frame_view_factory_views.cc').read_text()
        self.assertIn('std::make_unique<BrowserFrameViewLinux>', factory)
        self.assertRegex((FRAME / 'browser_frame_view_linux.h').read_text(), r'class BrowserFrameViewLinux\s*:\s*public OpaqueBrowserFrameView')
        opaque = (FRAME / 'opaque_browser_frame_view.cc').read_text()
        self.assertIn('return HTCLOSE;', extract(opaque, 'int OpaqueBrowserFrameView::NonClientHitTest('))

    def test_zero_opacity_takes_the_buttons_out_of_hit_testing_and_event_targeting(self):
        opaque = (FRAME / 'opaque_browser_frame_view.cc').read_text()
        opacity = extract(opaque, 'void OpaqueBrowserFrameView::SetCaptionButtonsOpacity(')
        self.assertIn('view->SetCanProcessEventsWithinSubtree(opacity > 0.0f);', opacity)
        self.assertIn('view->layer()->SetOpacity(opacity);', opacity)
        for button in ['minimize_button_', 'maximize_button_', 'restore_button_', 'close_button_']:
            self.assertIn(f'apply({button});', opacity)
        self.assertRegex(opaque, r'button->GetVisible\(\) &&\s*button->GetCanProcessEventsWithinSubtree\(\)')
        targeter = (TREE / 'ui/views/view_targeter_delegate.cc').read_text()
        self.assertIn('CanProcessEventsWithinSubtree()', targeter)

    def test_the_frame_view_is_reachable_through_the_public_browser_widget(self):
        widget = (FRAME / 'browser_widget.h').read_text()
        public = widget[widget.index(' public:'):]
        public = public[:public.index(' private:')] if ' private:' in public else public
        self.assertIn('BrowserFrameView* GetFrameView() const;', public)
        self.assertIn('BrowserWidget* browser_widget() const', (FRAME / 'browser_view.h').read_text())

    def test_only_zen_mode_restores_the_buttons(self):
        view = (FRAME / 'browser_view.cc').read_text()
        restores = [m.start() for m in re.finditer(r'SetCaptionButtonsOpacity\(', view)]
        self.assertTrue(restores)
        for start in restores:
            self.assertIn('zen', view[max(0, start - 2500):start].lower())


if __name__ == '__main__':
    unittest.main()
