import json
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

from test_downloads import compile_and_run
from test_renderer_contract import method
from test_session_merge import VALUES

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
LINUX = ROOT / 'src/ardia/linux'
FIXTURES = ROOT / 'tests/fixtures'

EXTENDED = VALUES.replace('#include <map>', '#include <map>\n#include <optional>', 1).replace(
    '''  const std::string* FindString(std::string_view key) const {
    auto* value = Find(key); return value && value->is_string() ? &value->GetString() : nullptr;
  }''', '''  const std::string* FindString(std::string_view key) const {
    auto* value = Find(key); return value && value->is_string() ? &value->GetString() : nullptr;
  }
  std::optional<double> FindDouble(std::string_view key) const {
    auto* value = Find(key); return value && (value->is_double() || value->is_int()) ? std::optional<double>(value->GetDouble()) : std::nullopt;
  }
  std::optional<int> FindInt(std::string_view key) const {
    auto* value = Find(key); return value && value->is_int() ? std::optional<int>(value->GetInt()) : std::nullopt;
  }
  std::optional<bool> FindBool(std::string_view key) const {
    auto* value = Find(key); return value && value->is_bool() ? std::optional<bool>(value->GetBool()) : std::nullopt;
  }
  Value* Set(std::string_view key, bool value) & { return Set(key, Value(value)); }
  DictValue&& Set(std::string_view key, bool value) && { Set(key, Value(value)); return std::move(*this); }
  Value* Set(std::string_view key, int value) & { return Set(key, Value(value)); }
  DictValue&& Set(std::string_view key, int value) && { Set(key, Value(value)); return std::move(*this); }
  Value* Set(std::string_view key, const char* value) & { return Set(key, Value(value)); }
  DictValue&& Set(std::string_view key, const char* value) && { Set(key, Value(value)); return std::move(*this); }''')


def stub_tree(root, names, include):
    for name in names:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f'#include "{include}"\n')


class ExtensionHelperTests(unittest.TestCase):
    def test_requests_manifests_listing_changes_prompts_and_surface_books(self):
        with tempfile.TemporaryDirectory(prefix='ardia-extensions-') as directory:
            root = pathlib.Path(directory)
            (root / 'base').mkdir()
            (root / 'base/values.h').write_text(VALUES)
            output = compile_and_run(self, 'extensions', [LINUX / 'ac_linux_extensions.cc', FIXTURES / 'extensions_scenarios.cc'],
                                     extra_includes=[root])
        self.assertIn('extensions scenarios passed', output)
        native = re.findall(r'^capability (\S+)$', output, re.M)
        script = (REPO / 'apps/linux/src/hostExtensionView.ts').read_text()
        self.assertEqual(sorted(native), sorted(set(re.findall(r'"(extensionView\.\w+)"', script))))
        self.assertEqual(len(native), 4)


class ExtensionSurfaceTests(unittest.TestCase):
    def test_popup_and_side_panel_hosts_follow_page_chrome_renderer_and_window_lifetimes(self):
        service = (LINUX / 'ac_linux_extension_service.cc').read_text()
        profile = method(service, 'Profile* LinuxExtensionViewProfile(')
        with tempfile.TemporaryDirectory(prefix='ardia-extension-views-') as directory:
            root = pathlib.Path(directory)
            (root / 'base').mkdir()
            (root / 'base/values.h').write_text(EXTENDED)
            stub_tree(root, [
                'base/functional/bind.h', 'base/functional/callback.h', 'base/logging.h', 'base/memory/raw_ptr.h',
                'base/memory/weak_ptr.h', 'base/scoped_observation.h', 'base/task/single_thread_task_runner.h',
                'base/time/time.h', 'base/timer/timer.h', 'chrome/browser/extensions/extension_view.h',
                'chrome/browser/extensions/extension_view_host.h', 'chrome/browser/extensions/extension_view_host_factory.h',
                'chrome/browser/profiles/profile.h', 'chrome/browser/ui/browser_window/public/browser_window_interface.h',
                'chrome/browser/ui/extensions/extension_popup_types.h', 'content/public/browser/render_frame_host.h',
                'content/public/browser/render_widget_host_view.h', 'content/public/browser/web_contents.h',
                'content/public/browser/web_contents_observer.h', 'extensions/browser/extension_host_registry.h',
                'extensions/browser/extension_registry.h', 'extensions/browser/extension_registry_observer.h',
                'extensions/common/extension.h', 'ui/gfx/geometry/rect.h', 'ui/gfx/geometry/rect_conversions.h',
                'ui/gfx/geometry/rect_f.h', 'ui/gfx/geometry/size.h', 'ui/gfx/native_ui_types.h',
                'ui/views/controls/webview/webview.h', 'ui/views/view.h', 'url/gurl.h',
            ], 'extension_views_support.h')
            resolver = root / 'profile.cc'
            resolver.write_text('#include "extension_views_support.h"\n#include "ardia/linux/ac_linux_extension_service.h"\n'
                                'namespace ardiacore {\nbool LinuxProfileHosts(std::string_view id, const Profile*) { return id.empty() || id == "default"; }\n}\n'
                                'namespace ardia {\n' + profile + '\n}\n')
            output = compile_and_run(self, 'extension-views', [
                LINUX / 'ac_linux_extension_views.cc', LINUX / 'ac_linux_extensions.cc', resolver,
                FIXTURES / 'extension_views_scenarios.cc',
            ], extra_includes=[root, FIXTURES])
        self.assertIn('extension views scenarios passed', output)


class ExtensionWiringTests(unittest.TestCase):
    def setUp(self):
        self.host = (LINUX / 'ac_linux_host.cc').read_text()
        self.service = (LINUX / 'ac_linux_extension_service.cc').read_text()

    def test_every_forwarded_management_call_has_an_engine_route(self):
        code = r'''
#include "ardia/linux/ac_linux_contract.h"
#include <cassert>
using namespace ardiacore::linux_contract;
int main() {
  for (auto name : {"extensions.list", "extensions.install", "extensions.installCrx", "extensions.setEnabled",
                    "extensions.uninstall", "extensions.reload", "extensions.configure", "extensions.watchActions",
                    "extensions.syncState", "extensions.syncApply", "search.engines"}) assert(FindRoute(name));
  for (auto name : {"extensions.actionStates", "extensions.sidePanelUrl", "extensions.inspectUnpacked",
                    "extensions.chooseFolder", "extensions.resolveInstallPrompt"}) assert(!FindRoute(name));
  assert(EventName("extensions.action") == "extensions.action" && EventName("extensions.sync") == "extensions.sync");
}
'''
        with tempfile.TemporaryDirectory(prefix='ardia-extension-routes-') as directory:
            source = pathlib.Path(directory) / 'routes.cc'
            source.write_text(code)
            compile_and_run(self, 'extension-routes', [source])

    def test_extension_calls_are_claimed_before_the_generic_engine_route_and_profile_gate(self):
        request = method(self.host, '  void Request(const base::ListValue& arguments) {')
        claim = request.index('if (LinuxExtensionsHandles(name))')
        self.assertLess(request.index('ParseProfileName(profile)'), claim)
        self.assertLess(claim, request.index('ardiacore::linux_contract::FindRoute(name)'))
        self.assertNotIn('ExtensionConfiguration', self.host)
        self.assertNotIn('reply->name == "search.engines"', self.host)
        for hook in ['g_ardia_extension_install_prompt = &InstallPromptHook', 'g_ardia_extension_side_panel = &SidePanelHook',
                     'g_ardia_open_action_popup = &ActionPopupHook']:
            self.assertIn(hook, self.service)
        start = method(self.host, 'void StartLinuxHost() {')
        self.assertLess(start.index('StartLinuxExtensions(std::move(extensions));'), start.index('ac_engine_set_event_sink(EngineEvent'))

    def test_window_and_renderer_retirement_release_surfaces_and_prompts(self):
        retire = method(self.host, '  void RetireRenderer() {')
        self.assertLess(retire.index('++renderer_generation_;'), retire.index('DetachViewports();'))
        detach = method(self.host, 'void ShellHostView::DetachViewports() {')
        self.assertIn('extension_surfaces_->RetireOlderThan(renderer_generation_)', detach)
        self.assertIn('LinuxExtensionsWindowRetired(id_);', detach)
        destructor = method(self.host, 'ShellHostView::~ShellHostView() {')
        self.assertIn('extension_surfaces_.reset();', destructor)
        self.assertLess(destructor.index('Hosts().erase(id_);'), destructor.index('LinuxExtensionsWindowRetired(id_);'))
        closed = method(self.host, '  void OnBrowserClosed(BrowserWindowInterface* browser) override {')
        self.assertLess(closed.index('found->second->ReleaseExtensions();'), closed.index('found->second->DetachBrowser();'))
        self.assertIn('extension_surfaces_.reset();', method(self.host, '  void ReleaseExtensions() {'))
        invoke = method(self.host, 'void ShellHostView::Invoke(')
        self.assertIn('LinuxExtensionSurfaces::Handles(method)', invoke)
        self.assertIn('renderer_generation_', invoke[invoke.index('LinuxExtensionSurfaces::Handles(method)'):])
        command = method(self.host, 'void ShellHostView::TabCommand(')
        self.assertIn('command == "executeExtensionAction"', command)
        self.assertIn('linux_extensions::kSurfaceCapabilities', method(self.host, 'base::DictValue ShellHostView::Bootstrap() const {'))

    def test_install_prompts_fall_back_to_chromium_and_are_never_silently_accepted(self):
        hook = method(self.service, 'bool InstallPromptHook(')
        self.assertIn('if (!window) return false;', hook)
        self.assertIn('if (pending) *done = std::move(pending->done);', hook)
        self.assertIn('InstallResult::ABORTED', hook)
        self.assertNotIn('ACCEPTED', hook)
        resolve = method(self.service, 'void ResolvePrompt(')
        self.assertIn('case ext::PromptResolve::kWrongWindow:', resolve)
        retarget = method(self.service, 'void Retarget(int window) {')
        self.assertIn('InstallResult::USER_CANCELED', retarget)

    def test_gn_lists_extension_helpers_and_their_dependencies(self):
        build = (LINUX / 'BUILD.gn').read_text()
        target = build[build.index('source_set("linux")'):]
        for name in ['ac_linux_extension_service', 'ac_linux_extension_views', 'ac_linux_extensions']:
            for suffix in ['.cc', '.h']:
                self.assertIn(f'"{name}{suffix}"', target)
        for dep in ['//chrome/browser/extensions', '//extensions/common', '//extensions/browser', '//ui/shell_dialogs']:
            self.assertIn(f'"{dep}"', target)

    def test_action_popup_hook_patch_is_registered_for_linux_and_applies(self):
        series = (REPO / 'engine/patches/series').read_text()
        self.assertIn('chromium chromium-linux-action-popup-hook.patch linux', series)
        patch = REPO / 'engine/patches/chromium-linux-action-popup-hook.patch'
        text = patch.read_text()
        self.assertIn('g_ardia_open_action_popup', text)
        self.assertNotRegex('\n'.join(line for line in text.splitlines() if line.startswith('+')), r'^\+\s*(//|/\*)')
        source = pathlib.Path.home() / 'ardia-linux-build-156/chromium_git/chromium/src/chrome/browser/extensions/api/extension_action/extension_action_api.cc'
        if not source.exists() or shutil.which('patch') is None:
            self.skipTest('Chromium 156 source is unavailable')
        with tempfile.TemporaryDirectory(prefix='ardia-action-popup-patch-') as directory:
            target = pathlib.Path(directory) / 'chrome/browser/extensions/api/extension_action/extension_action_api.cc'
            target.parent.mkdir(parents=True)
            target.write_text(source.read_text())
            result = subprocess.run(['patch', '-p1', '--dry-run', '-F0', '-i', str(patch)], cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
