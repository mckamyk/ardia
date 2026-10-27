import pathlib
import re
import subprocess
import tempfile
import unittest

from test_downloads import compile_and_run
from test_session_merge import VALUES

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/ardia/linux'
HOST = (LINUX / 'ac_linux_host.cc').read_text()


def body(source, start):
    begin = source.index(start)
    depth = 0
    for index in range(source.index('{', begin), len(source)):
        depth += {'{': 1, '}': -1}.get(source[index], 0)
        if depth == 0:
            return source[begin:index + 1]
    raise AssertionError(f'unterminated {start}')


class SiteControlsHelperTests(unittest.TestCase):
    def test_permissions_data_clearing_and_blocker_state_machine(self):
        with tempfile.TemporaryDirectory(prefix='ardia-site-controls-') as directory:
            include = pathlib.Path(directory) / 'include'
            (include / 'base').mkdir(parents=True)
            (include / 'base/values.h').write_text(VALUES)
            output = compile_and_run(self, 'site-controls', [
                LINUX / 'ac_linux_site_controls.cc', ROOT / 'tests/fixtures/site_controls_scenarios.cc',
            ], extra_includes=[include])
        self.assertIn('site controls scenarios passed', output)

    def test_service_drives_engine_calls_component_load_and_the_bridge_page(self):
        service = [LINUX / 'ac_linux_site_controls_service.cc', LINUX / 'ac_linux_site_controls_service.h']
        includes = set()
        for path in service:
            includes.update(re.findall(r'^#include "([^"]+)"', path.read_text(), re.M))
        with tempfile.TemporaryDirectory(prefix='ardia-site-controls-stubs-') as directory:
            stubs = pathlib.Path(directory)
            for name in includes:
                if name.startswith('ardia/linux/'):
                    continue
                (stubs / name).parent.mkdir(parents=True, exist_ok=True)
                (stubs / name).write_text(VALUES if name == 'base/values.h' else '#include "site_controls_service_support.h"\n')
            (stubs / 'base').mkdir(exist_ok=True)
            (stubs / 'base/values.h').write_text(VALUES)
            output = compile_and_run(self, 'site-controls-service', [
                LINUX / 'ac_linux_site_controls.cc', LINUX / 'ac_linux_site_controls_service.cc',
                LINUX / 'ac_linux_profiles.cc', ROOT / 'tests/fixtures/site_controls_service_scenarios.cc',
            ], extra_includes=[stubs, ROOT / 'tests/fixtures'])
        self.assertIn('site controls service scenarios passed', output)

    def test_bridge_page_against_a_simulated_ublock_dashboard(self):
        result = subprocess.run(['node', '--test', str(ROOT / 'tests/content_blocker_bridge.test.mjs')],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertRegex(result.stdout, r'pass 8\b')


class SiteControlsWiringTests(unittest.TestCase):
    def test_site_calls_pass_the_profile_name_gate_then_reach_the_helper_before_the_route_table(self):
        request = body(HOST, '  void Request(const base::ListValue& arguments) {')
        intercept = request.index('if (LinuxSiteControlsHandles(name))')
        self.assertLess(request.index('ardiacore::linux_profiles::ParseProfileName(profile)'), intercept)
        self.assertLess(request.index('if (name == "profiles.release")'), intercept)
        self.assertLess(intercept, request.index('ardiacore::linux_contract::FindRoute(name)'))
        self.assertLess(intercept, request.index('ardiacore::ResolveLinuxProfile(profile'))
        self.assertIn('LinuxSiteControlsCall(host_->profile(), name, profile, args.Find("args")', request)
        contract = (LINUX / 'ac_linux_contract.h').read_text()
        for name in ['sites.settings', 'sites.setSetting', 'sites.origins', 'sites.reset', 'sites.clearData',
                     'profiles.clearBrowsingData', 'contentBlocker.']:
            self.assertNotIn(f'"{name}', contract)

    def test_blocker_starts_with_each_window_and_broadcasts_changes_to_its_profile(self):
        self.assertIn('StartLinuxContentBlocker(profile());', body(HOST, 'void ShellHostView::Initialize() {'))
        self.assertIn('  AppendLinuxSiteControlsCapabilities(capabilities);\n  auto bootstrap = base::DictValue()', HOST)
        start = body(HOST, 'void StartLinuxHost() {')
        sink = start[start.index('StartLinuxSiteControls('):]
        self.assertIn('host->profile()->GetOriginalProfile() != profile', sink)
        self.assertIn('host->Event("ArdiaContentBlocker", "contentBlocker"', sink)
        self.assertIn('.Set("name", "contentBlocker.changed")', sink)

    def test_service_restricts_the_component_and_drives_only_its_own_bridge_page(self):
        service = (LINUX / 'ac_linux_site_controls_service.cc').read_text()
        hook = body(service, 'void StartLinuxSiteControls(ContentBlockerChanged changed) {')
        self.assertIn('g_ardia_restricted_component_extension = ', hook)
        self.assertIn('return id == controls::kExtensionId;', hook)
        frame = body(service, '  content::RenderFrameHost* BridgeFrame() const {')
        self.assertIn('frame->GetLastCommittedURL() != BridgeUrl()', frame)
        self.assertEqual(service.count('ExecuteJavaScriptInIsolatedWorld('), 2)
        self.assertEqual(service.count('ISOLATED_WORLD_ID_CHROME_INTERNAL'), 2)
        self.assertNotIn('ExecuteJavaScript(', service.replace('ExecuteJavaScriptInIsolatedWorld(', ''))
        self.assertIn('if (!profile || profile->IsOffTheRecord()) return;', body(service, 'void StartLinuxContentBlocker(Profile* profile) {'))
        resolve = body(service, 'void SiteControlsProfile(')
        self.assertIn('controls::RouteProfile(', resolve)
        self.assertIn('ardiacore::ResolveLinuxProfile(requested', resolve)
        self.assertIn('IsOffTheRecord()) error = ProfileError::kPrivateService', resolve)
        self.assertEqual(service.count('SiteControlsProfile('), 2)

    def test_gn_lists_the_helper_and_its_extension_dependency(self):
        build = (LINUX / 'BUILD.gn').read_text()
        target = build[build.index('source_set("linux")'):]
        for name in ['ac_linux_site_controls.cc', 'ac_linux_site_controls.h',
                     'ac_linux_site_controls_service.cc', 'ac_linux_site_controls_service.h']:
            self.assertIn(f'"{name}"', target)
        self.assertIn('"//chrome/browser/extensions"', target)

    def test_bridge_files_carry_no_comments(self):
        for path in (ROOT / 'content-blocker').iterdir():
            text = path.read_text()
            self.assertNotIn('<!--', text, path)
            self.assertIsNone(re.search(r'(^|\s)//(?!/)', text), path)
            self.assertNotIn('/*', text, path)


if __name__ == '__main__':
    unittest.main()
