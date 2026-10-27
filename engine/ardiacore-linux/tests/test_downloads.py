import json
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

from test_session_merge import VALUES

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/ardia/linux'


def compile_and_run(test, name, sources, args=(), extra_includes=()):
    compiler = shutil.which('clang++') or shutil.which('c++')
    if compiler is None:
        test.skipTest('C++ compiler is unavailable')
    with tempfile.TemporaryDirectory(prefix=f'ardia-{name}-') as directory:
        root = pathlib.Path(directory)
        executable = root / name
        compiled = subprocess.run([
            compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-g',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            *[f'-I{path}' for path in extra_includes], '-I', str(ROOT / 'src'),
            *[str(source) for source in sources], '-o', str(executable),
        ], capture_output=True, text=True, timeout=120)
        test.assertEqual(compiled.returncode, 0, compiled.stderr)
        result = subprocess.run([str(executable), *args], capture_output=True, text=True, timeout=30)
        test.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout


class DownloadHelperTests(unittest.TestCase):
    def test_ids_paths_requests_snapshots_trash_ledger_and_file_gates(self):
        with tempfile.TemporaryDirectory(prefix='ardia-downloads-') as directory:
            root = pathlib.Path(directory)
            (root / 'include/base').mkdir(parents=True)
            (root / 'include/base/values.h').write_text(VALUES)
            (root / 'scratch').mkdir()
            output = compile_and_run(self, 'downloads', [
                LINUX / 'ac_linux_downloads.cc', LINUX / 'ac_linux_file_drop.cc',
                ROOT / 'tests/fixtures/downloads_scenarios.cc',
            ], args=[str(root / 'scratch')], extra_includes=[root / 'include'])
        self.assertIn('downloads scenarios passed', output)
        native = re.findall(r'^capability (\S+)$', output, re.M)
        script = "import('./src/fileCapabilities.ts').then((m) => console.log(JSON.stringify(m.FILE_CAPABILITIES)))"
        result = subprocess.run(['node', '--experimental-strip-types', '--input-type=module', '-e', script],
                                cwd=ROOT.parents[1] / 'apps/linux', capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(native), 3)
        self.assertEqual(sorted(native), sorted(json.loads(result.stdout)))


class DownloadServiceTests(unittest.TestCase):
    def test_service_waits_for_history_and_clears_with_the_listed_filter(self):
        with tempfile.TemporaryDirectory(prefix='ardia-download-stubs-') as directory:
            stubs = pathlib.Path(directory)
            support = '#include "download_service_support.h"\n'
            for name in ['base/functional/callback.h', 'base/functional/bind.h', 'base/location.h',
                         'base/scoped_observation.h', 'base/time/time.h', 'base/timer/timer.h',
                         'base/files/file_path.h', 'components/download/public/common/download_item.h',
                         'content/public/browser/download_manager.h']:
                (stubs / name).parent.mkdir(parents=True, exist_ok=True)
                (stubs / name).write_text(support)
            output = compile_and_run(self, 'download-service', [
                LINUX / 'ac_linux_download_service.cc', ROOT / 'tests/fixtures/download_service_scenarios.cc',
            ], extra_includes=[stubs, ROOT / 'tests/fixtures'])
        self.assertIn('download service scenarios passed', output)


class DownloadWiringTests(unittest.TestCase):
    def test_contract_routes_engine_controls_and_forwards_removals(self):
        code = r'''
#include "ardia/linux/ac_linux_contract.h"
#include <cassert>
#include <string_view>
using namespace ardiacore::linux_contract;
void Expect(std::string_view name, std::string_view service, std::string_view field) {
  const Route* route = FindRoute(name);
  assert(route && route->service == service && route->field == field);
}
int main() {
  Expect("downloads.cancel", "ac_downloads_cancel", "null");
  Expect("downloads.pause", "ac_downloads_pause", "null");
  Expect("downloads.resume", "ac_downloads_resume", "null");
  Expect("downloads.keep", "ac_downloads_keep", "null");
  Expect("downloads.settings", "ac_downloads_settings", "");
  Expect("downloads.setSettings", "ac_downloads_set_settings", "");
  assert(!FindRoute("downloads.list") && !FindRoute("downloads.remove") && !FindRoute("downloads.clear"));
  assert(EventName("downloads.changed") == "downloads.changed");
  assert(EventName("downloads.removed") == "downloads.removed");
}
'''
        with tempfile.TemporaryDirectory(prefix='ardia-download-contract-') as directory:
            source = pathlib.Path(directory) / 'contract.cc'
            source.write_text(code)
            compile_and_run(self, 'download-contract', [source])

    def test_gn_target_lists_every_linux_host_source_that_exists(self):
        build = (LINUX / 'BUILD.gn').read_text()
        target = build[build.index('source_set("linux")'):]
        listed = set(re.findall(r'"(ac_linux_\w+\.(?:cc|h))"', target[:target.index('sources +=')]))
        closing = set(re.findall(r'"(ac_linux_\w+\.(?:cc|h))"', build[build.index('source_set("close_hooks")'):build.index('source_set("linux")')]))
        present = {path.name for path in LINUX.glob('ac_linux_*') if path.suffix in {'.cc', '.h'}}
        self.assertLessEqual(listed | closing, present)
        self.assertLessEqual({name for name in present if name.endswith('.cc')}, listed | closing)
        for name in ['ac_linux_download_service', 'ac_linux_downloads', 'ac_linux_file_drop', 'ac_linux_files']:
            self.assertLessEqual({f'{name}.cc', f'{name}.h'}, listed)
        for dep in ['//components/download/public/common', '//chrome/browser/download', '//extensions/browser']:
            self.assertIn(f'"{dep}"', target)

if __name__ == '__main__':
    unittest.main()
