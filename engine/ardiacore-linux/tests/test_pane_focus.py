import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HOST = ROOT / 'src/ardia/linux/ac_linux_host.cc'
FIXTURE = ROOT / 'tests/fixtures/pane_focus.h'


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


MAIN = r'''
struct Pane {
  content::WebContents page, frontend;
  views::WebView viewport, tools;
  TabBinding* binding;
};

struct Split {
  ShellHostView host;
  Pane a, b;
  views::View shell;
  Split() {
    ChromeWebContentsViewFocusHelper::helpers.clear();
    views::focus_manager.focused = nullptr;
    Bind(a, 1);
    Bind(b, 2);
  }
  void Bind(Pane& pane, int id) {
    auto binding = std::make_unique<TabBinding>();
    pane.viewport.contents = &pane.page;
    pane.tools.contents = &pane.frontend;
    binding->contents_ = &pane.page;
    binding->viewport_ = &pane.viewport;
    pane.binding = binding.get();
    host.bindings_[id] = std::move(binding);
  }
  void ChromeActivates(Pane& from, Pane& to) {
    ChromeWebContentsViewFocusHelper::FromWebContents(&from.page)->StoreFocus();
    views::focus_manager.focused = nullptr;
    if (!ChromeWebContentsViewFocusHelper::FromWebContents(&to.page)->RestoreFocus()) to.page.Focus();
  }
  void ShellActivates(Pane& from, Pane& to) {
    host.KeepPaneFocus(to.binding);
    ChromeActivates(from, to);
    host.KeepPaneFocus(nullptr);
  }
  void Click(Pane& pane) { pane.viewport.RequestFocus(); }
};

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string mode = argv[1];
  if (mode == "click") {
    Split s;
    s.Click(s.a);
    s.ChromeActivates(s.a, s.b);
    s.Click(s.b);
    const int a = s.a.page.focuses, b = s.b.page.focuses;
    s.Click(s.a);
    s.ShellActivates(s.b, s.a);
    assert(views::focus_manager.focused == &s.a.viewport);
    assert(s.b.page.focuses == b && s.a.page.focuses == a + 2);
    s.Click(s.b);
    s.ShellActivates(s.a, s.b);
    assert(views::focus_manager.focused == &s.b.viewport && s.a.page.focuses == a + 2);
    return 0;
  }
  if (mode == "sidebar") {
    Split s;
    s.Click(s.b);
    s.ShellActivates(s.a, s.b);
    s.Click(s.a);
    s.ShellActivates(s.b, s.a);
    const int a = s.a.page.focuses;
    s.shell.RequestFocus();
    s.ShellActivates(s.a, s.b);
    assert(views::focus_manager.focused == &s.b.viewport && s.a.page.focuses == a);
    const int b = s.b.page.focuses;
    s.shell.RequestFocus();
    s.ShellActivates(s.b, s.a);
    assert(views::focus_manager.focused == &s.shell && s.b.page.focuses == b);
    return 0;
  }
  if (mode == "devtools") {
    Split s;
    s.a.binding->devtools_viewport_ = &s.a.tools;
    s.Click(s.b);
    s.ChromeActivates(s.a, s.b);
    s.a.tools.RequestFocus();
    const int b = s.b.page.focuses;
    s.ShellActivates(s.b, s.a);
    assert(views::focus_manager.focused == &s.a.tools && s.b.page.focuses == b);
    s.Click(s.b);
    s.ShellActivates(s.a, s.b);
    s.a.viewport.visible = false;
    s.a.tools.visible = false;
    const int hidden = s.b.page.focuses;
    s.shell.RequestFocus();
    s.ShellActivates(s.b, s.a);
    assert(views::focus_manager.focused != &s.b.viewport && s.b.page.focuses == hidden);
    return 0;
  }
  assert(false);
}
'''


class PaneFocusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('CXX', 'clang++')) or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('C++ compiler unavailable')
        cls.host = HOST.read_text()
        code = FIXTURE.read_text().replace('#pragma once\n', '').replace(
            'ARDIA_HOST_METHODS', method(cls.host, 'void ShellHostView::KeepPaneFocus('))
        cls.temp = tempfile.TemporaryDirectory(prefix='ardia-pane-focus-')
        cls.addClassCleanup(cls.temp.cleanup)
        root = pathlib.Path(cls.temp.name)
        (root / 'main.cc').write_text(code + MAIN)
        cls.binary = root / 'pane-focus'
        subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        str(root / 'main.cc'), '-o', str(cls.binary)], check=True, timeout=120)

    def run_case(self, mode):
        subprocess.run([str(self.binary), mode], check=True, timeout=20)

    def test_clicking_the_inactive_pane_keeps_focus_there_through_chromes_restore(self):
        self.run_case('click')

    def test_activating_a_pane_from_the_shell_focuses_that_pane_and_never_its_neighbour(self):
        self.run_case('sidebar')

    def test_a_focused_docked_devtools_frontend_survives_activating_its_pane(self):
        self.run_case('devtools')

    def test_shell_activation_and_chromes_active_tab_change_keep_pane_focus(self):
        strip = method(self.host, 'void ShellHostView::StripCommand(')
        self.assertLess(strip.index('KeepPaneFocus(binding);'), strip.index('model->ActivateTabAt(index);'))
        changed = method(self.host, 'void ShellHostView::OnTabStripModelChanged(')
        self.assertIn('if (selection.active_tab_changed()) KeepPaneFocus(nullptr);', changed)


if __name__ == '__main__':
    unittest.main()
