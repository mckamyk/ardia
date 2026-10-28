import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
PATCH = REPO / 'engine/patches/chromium-linux-devtools-open-tab.patch'
HOST = ROOT / 'src/ardia/linux/ac_linux_host.cc'
TREE = pathlib.Path(os.environ.get('ARDIA_CHROMIUM_156_SRC', pathlib.Path.home() / 'ardia-linux-build-156/chromium_git/chromium/src'))


def block(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def added(patch):
    return '\n'.join(line[1:] for line in patch.splitlines() if line.startswith('+') and not line.startswith('+++'))


PRELUDE = r'''
#include <cassert>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#define BUILDFLAG(flag) 1
#define LOG(level) std::ostringstream()
struct GURL {
  std::string spec;
  bool operator==(const GURL&) const = default;
};
namespace content {
struct OpenURLParams { GURL url; };
struct WebContents {
  WebContents* OpenURL(const OpenURLParams& params, int) {
    opened_urls.push_back(params.url);
    return result;
  }
  WebContents* result = nullptr;
  std::vector<GURL> opened_urls;
};
}
using content::WebContents;
struct Report {
  content::WebContents* opened;
  GURL url;
  std::string disposition;
};
class TabBinding {
 public:
  explicit TabBinding(content::WebContents* contents) : contents_(contents) {}
  content::WebContents* contents() const { return contents_; }
  void ReportOpened(content::WebContents* opened, const GURL& url, std::string_view disposition) {
    reports.push_back({opened, url, std::string(disposition)});
  }
  std::vector<Report> reports;
 private:
  content::WebContents* contents_;
};
class ShellHostView;
std::map<int, ShellHostView*>& Hosts() {
  static std::map<int, ShellHostView*> hosts;
  return hosts;
}
class ShellHostView {
 public:
  explicit ShellHostView(int id) { Hosts()[id] = this; }
  ~ShellHostView() { Hosts().clear(); }
  TabBinding* Bind(int id, content::WebContents* contents) {
    bindings_[id] = std::make_unique<TabBinding>(contents);
    return bindings_[id].get();
  }
  ARDIA_REPORT_METHOD
 private:
  std::map<int, std::unique_ptr<TabBinding>> bindings_;
};
void (*g_ardia_linux_devtools_opened_tab)(content::WebContents*, content::WebContents*, const GURL&) = nullptr;
void InstallHook() {
  ARDIA_HOOK
}
int fallbacks = 0;
void OpenInNewTab(content::WebContents* inspected_web_contents, const GURL& fixed_url) {
  content::OpenURLParams params{fixed_url};
ARDIA_OPEN_IN_NEW_TAB
    fallbacks++;
  }
}
'''

MAIN = r'''
int main() {
  content::WebContents first_page, second_page, opened, stranger;
  ShellHostView first(1), second(2);
  TabBinding* a = first.Bind(7, &first_page);
  TabBinding* b = second.Bind(9, &second_page);
  InstallHook();
  assert(g_ardia_linux_devtools_opened_tab);
  second_page.result = &opened;
  OpenInNewTab(&second_page, {"https://example.test/5"});
  assert(second_page.opened_urls.size() == 1 && fallbacks == 0);
  assert(a->reports.empty() && b->reports.size() == 1);
  assert(b->reports[0].opened == &opened && b->reports[0].url == GURL{"https://example.test/5"});
  assert(b->reports[0].disposition == "foreground");
  second_page.result = &second_page;
  OpenInNewTab(&second_page, {"https://example.test/same"});
  assert(b->reports.size() == 1 && fallbacks == 0);
  second_page.result = nullptr;
  OpenInNewTab(&second_page, {"https://example.test/refused"});
  assert(b->reports.size() == 1 && fallbacks == 1);
  OpenInNewTab(nullptr, {"https://example.test/none"});
  assert(fallbacks == 2);
  stranger.result = &opened;
  OpenInNewTab(&stranger, {"https://example.test/stranger"});
  assert(a->reports.empty() && b->reports.size() == 1 && fallbacks == 2);
  assert(!first.ReportDevToolsOpenedTab(nullptr, &opened, {"https://example.test"}));
  return 0;
}
'''


class DevToolsOpenTabTests(unittest.TestCase):
    def setUp(self):
        self.patch = PATCH.read_text()
        self.host = HOST.read_text()

    def linux_open_block(self):
        hunk = next(part for part in re.split(r'^@@.*$', self.patch, flags=re.M) if 'OpenURL(params' in part)
        lines = [line[1:] for line in hunk.splitlines() if line[:1] in (' ', '+')]
        text = '\n'.join(lines)
        start = text.index('#if BUILDFLAG(IS_LINUX)\n') + len('#if BUILDFLAG(IS_LINUX)\n')
        return text[start:text.index('\n#else', start)]

    def test_the_patch_reports_the_tab_devtools_opened_and_keeps_chromes_fallback(self):
        self.assertTrue(self.patch.startswith('--- a/chrome/browser/devtools/devtools_window.cc\n+++ b/chrome/browser/devtools/devtools_window.cc\n'))
        lines = added(self.patch)
        self.assertIn('void (*g_ardia_linux_devtools_opened_tab)(WebContents* inspected,', lines)
        self.assertEqual(lines.count('#if BUILDFLAG(IS_LINUX)'), 2)
        self.assertEqual(lines.count('#else'), 1)
        self.assertNotRegex(lines, r'(^|\s)//|/\*')
        self.assertIn('  if (!linux_opened) {', self.linux_open_block())

    def test_the_patch_is_registered_for_linux_after_the_devtools_authority_patch(self):
        series = (REPO / 'engine/patches/series').read_text().splitlines()
        self.assertIn('chromium chromium-linux-devtools-open-tab.patch linux', series)
        self.assertLess(series.index('chromium chromium-linux-devtools-authority.patch linux'),
                        series.index('chromium chromium-linux-devtools-open-tab.patch linux'))

    def test_the_patch_applies_to_the_156_tree(self):
        target = TREE / 'chrome/browser/devtools/devtools_window.cc'
        if not target.is_file() or not shutil.which('patch'):
            self.skipTest('Chromium 156 tree or GNU patch unavailable')
        with tempfile.TemporaryDirectory(prefix='ardia-devtools-open-tab-') as scratch:
            copy = pathlib.Path(scratch) / 'chrome/browser/devtools/devtools_window.cc'
            copy.parent.mkdir(parents=True)
            shutil.copyfile(target, copy)
            result = subprocess.run(['patch', '-p1', '--forward', '--batch', '-i', str(PATCH)], cwd=scratch,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('g_ardia_linux_devtools_opened_tab(inspected_web_contents, linux_opened,', copy.read_text())

    def test_devtools_opened_tabs_are_reported_from_the_inspected_tabs_binding(self):
        compiler = shutil.which(os.environ.get('CXX', 'clang++')) or shutil.which('g++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        self.assertIn('extern void (*g_ardia_linux_devtools_opened_tab)(content::WebContents*, content::WebContents*, const GURL&);', self.host)
        report = block(self.host, '  bool ReportDevToolsOpenedTab(')
        hook = self.host[self.host.index('  g_ardia_linux_devtools_opened_tab = '):]
        hook = hook[:hook.index('\n  };\n') + len('\n  };\n')]
        code = (PRELUDE.replace('ARDIA_REPORT_METHOD', report).replace('ARDIA_HOOK', hook)
                .replace('ARDIA_OPEN_IN_NEW_TAB', self.linux_open_block())) + MAIN
        with tempfile.TemporaryDirectory(prefix='ardia-devtools-open-tab-') as scratch:
            path = pathlib.Path(scratch)
            (path / 'main.cc').write_text(code)
            result = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wno-unused-value',
                                     '-fsanitize=address,undefined', str(path / 'main.cc'), '-o', str(path / 'test')],
                                    capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / 'test')], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
