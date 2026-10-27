import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

from test_session_merge import VALUES

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/ardia/linux'


class DownloadHelperTests(unittest.TestCase):
    def test_ids_paths_requests_trash_and_drop_ledger(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        with tempfile.TemporaryDirectory(prefix='ardia-downloads-') as directory:
            root = pathlib.Path(directory)
            headers = root / 'include'
            (headers / 'base').mkdir(parents=True)
            (headers / 'base/values.h').write_text(VALUES)
            executable = root / 'downloads'
            compiled = subprocess.run([
                compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-g',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-I', str(headers), '-I', str(ROOT / 'src'),
                str(LINUX / 'ac_linux_downloads.cc'), str(LINUX / 'ac_linux_file_drop.cc'),
                str(ROOT / 'tests/fixtures/downloads_scenarios.cc'),
                '-o', str(executable),
            ], capture_output=True, text=True, timeout=120)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            scratch = root / 'scratch'
            scratch.mkdir()
            result = subprocess.run([str(executable), str(scratch)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('downloads scenarios passed', result.stdout)


class DownloadWiringTests(unittest.TestCase):
    def test_contract_routes_every_download_control_and_removal_event(self):
        source = (LINUX / 'ac_linux_contract.h').read_text()
        routes = set(re.findall(r'\{"(downloads\.\w+)", "(ac_\w+)", "(\w*)"\}', source))
        for verb, field in [('cancel', 'null'), ('pause', 'null'), ('resume', 'null'), ('keep', 'null'),
                            ('remove', 'null'), ('list', 'downloads'), ('settings', ''), ('setSettings', '')]:
            service = 'ac_downloads_' + re.sub(r'([A-Z])', lambda m: '_' + m.group(1).lower(), verb)
            self.assertIn((f'downloads.{verb}', service, field), routes)
        events = source[source.index('EventName('):]
        self.assertIn('"downloads.changed"', events)
        self.assertIn('"downloads.removed"', events)

    def test_host_validates_download_requests_before_routing(self):
        source = (LINUX / 'ac_linux_host.cc').read_text()
        request = source[source.index('void Request(const base::ListValue& arguments)'):]
        request = request[:request.index('\n  base::WeakPtr<ShellHostView> host_;')]
        validation = request.index('linux_downloads::ValidateRequest(name, input)')
        self.assertLess(validation, request.index('ac_engine_call(service.c_str()'))
        self.assertLess(request.index('const auto& input = parsed->GetDict();'), validation)

    def test_host_routes_file_services_through_the_shell_view(self):
        source = (LINUX / 'ac_linux_host.cc').read_text()
        self.assertIn('#include "ardia/linux/ac_linux_files.h"', source)
        self.assertIn('raw_ptr<LinuxShellWebView> shell_', source)
        self.assertIn('std::make_unique<LinuxShellWebView>(browser->GetProfile())', source)
        invoke = source[source.index('void ShellHostView::Invoke('):]
        self.assertIn('if (LinuxFilesHandles(method)) return LinuxFilesInvoke(profile(), shell_, method, values, std::move(done));', invoke)
        bootstrap = source[source.index('base::DictValue ShellHostView::Bootstrap() const'):]
        bootstrap = bootstrap[:bootstrap.index('\n}\n')]
        for capability in ['ArdiaSystem.fileStatus', 'ArdiaFileDrop.takeDrop', 'ArdiaFileDrag.begin']:
            self.assertIn(f'"{capability}"', bootstrap)

    def test_build_lists_the_download_helpers(self):
        build = (LINUX / 'BUILD.gn').read_text()
        for name in ['ac_linux_downloads.cc', 'ac_linux_downloads.h', 'ac_linux_file_drop.cc',
                     'ac_linux_file_drop.h', 'ac_linux_files.cc', 'ac_linux_files.h']:
            self.assertIn(f'"{name}"', build)
        self.assertIn('"//components/download/public/common"', build)

    def test_file_service_validates_paths_against_profile_downloads(self):
        source = (LINUX / 'ac_linux_files.cc').read_text()
        self.assertIn('PathListed(', source)
        self.assertIn('GetAllDownloads(', source)
        self.assertIn('RunDragDropLoop(', source)
        self.assertIn('ChooseRevealBackend(', source)
        self.assertIn('drops_.Record(', source)
        self.assertNotRegex(source, r'(?m)^\s*//')


if __name__ == '__main__':
    unittest.main()
