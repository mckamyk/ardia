import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

LINUX = pathlib.Path(__file__).resolve().parents[4] / 'scripts/linux'
sys.path.insert(0, str(LINUX))
import runtime_files as runtime
import linux_platform


class RuntimeArchitectureTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.scratch.name)
        source = self.root / 'fixture.c'
        source.write_text('int main(void) { return 0; }\n')
        subprocess.run(['cc', '-Wl,--build-id', str(source), '-o', str(self.root / 'fixture')], check=True)
        self.elf = (self.root / 'fixture').read_bytes()
        self.native_cpu = linux_platform.elf_cpu(self.root / 'fixture')
        self.other_cpu = 'arm64' if self.native_cpu == 'x64' else 'x64'
        self.out = self.root / 'out'
        self.out.mkdir()
        for name in runtime.REQUIRED:
            path = self.out / name
            if name == 'locales':
                path.mkdir()
            else:
                path.write_bytes(self.elf if name in ('chrome', 'chrome_crashpad_handler') else b'fixture data')
        (self.out / 'chrome').chmod(0o755)

    def tearDown(self):
        self.scratch.cleanup()

    def machine_header_fixture(self, cpu):
        data = bytearray(self.elf)
        struct.pack_into('<H', data, 18, {'x64': 62, 'arm64': 183}[cpu])
        return bytes(data)

    def test_select_rejects_arbitrary_executable_bytes(self):
        (self.out / 'chrome').write_bytes(b'not an ELF engine')
        with self.assertRaisesRegex(ValueError, 'ELF'):
            runtime.select(self.out)

    def test_select_rejects_wrong_arch_and_mixed_libraries(self):
        with self.assertRaisesRegex(ValueError, self.other_cpu):
            runtime.select(self.out, self.other_cpu)
        (self.out / 'libEGL.so').write_bytes(self.machine_header_fixture(self.other_cpu))
        with self.assertRaisesRegex(ValueError, 'libEGL'):
            runtime.select(self.out, self.native_cpu)

    def test_arm64_machine_header_fixture_and_archive_names(self):
        for name in ('chrome', 'chrome_crashpad_handler'):
            (self.out / name).write_bytes(self.machine_header_fixture('arm64'))
        self.assertIn('chrome', runtime.select(self.out, 'aarch64'))
        self.assertEqual(runtime.archive_name('a' * 40, 'arm64'), 'ArdiaCore-aaaaaaaaaaaa-linux-arm64.tar.xz')
        self.assertEqual(runtime.archive_root('x86_64'), 'ardiacore-linux-x64')
        with self.assertRaises(ValueError):
            runtime.archive_root('../arm64')

    def test_manifest_validates_digest_and_actual_architecture(self):
        manifest = {'format': 2, 'engine_key': 'a' * 40, 'target_os': 'linux', 'target_cpu': self.native_cpu, 'chrome_sha256': hashlib.sha256(self.elf).hexdigest()}
        path = self.out / 'manifest.json'
        path.write_text(json.dumps(manifest))
        runtime.check_manifest(self.out, 'a' * 40, self.native_cpu)
        manifest['chrome_sha256'] = '0' * 64
        path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            runtime.check_manifest(self.out, 'a' * 40, self.native_cpu)
        manifest.update(target_cpu=self.other_cpu, chrome_sha256=hashlib.sha256(self.elf).hexdigest())
        path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, self.other_cpu):
            runtime.check_manifest(self.out, 'a' * 40, self.other_cpu)

    def test_select_rejects_mislabeled_manifest(self):
        (self.out / 'manifest.json').write_text(json.dumps({'target_os': 'linux', 'target_cpu': self.other_cpu}))
        with self.assertRaisesRegex(ValueError, 'manifest'):
            runtime.select(self.out)

    def test_manifest_requires_an_object(self):
        (self.out / 'manifest.json').write_text('[]')
        with self.assertRaisesRegex(ValueError, 'manifest'):
            runtime.check_manifest(self.out, 'a' * 40, self.native_cpu)

    def test_cross_strip_never_uses_native_strip(self):
        with mock.patch.object(linux_platform.platform, 'machine', return_value='x86_64'), mock.patch.object(linux_platform.shutil, 'which', side_effect=lambda name: '/usr/bin/strip' if name == 'strip' else None):
            self.assertEqual(linux_platform.strip_tool('x64'), '/usr/bin/strip')
            with self.assertRaisesRegex(ValueError, 'aarch64-linux-gnu-strip'):
                linux_platform.strip_tool('arm64')

    def test_truncated_elf_and_object_file_are_rejected(self):
        for data in (self.elf[:64], self.elf[:18] + b'\x00\x00' + self.elf[20:]):
            (self.out / 'chrome').write_bytes(data)
            with self.assertRaisesRegex(ValueError, 'ELF'):
                runtime.select(self.out)


class RuntimeHostSimulationTests(unittest.TestCase):
    def test_arm64_compiler_header_fixture_suite(self):
        native_run = subprocess.run

        def simulated_arm64_compiler(command, **kwargs):
            result = native_run(command, **kwargs)
            if command[0] == 'cc' and result.returncode == 0:
                output = pathlib.Path(command[command.index('-o') + 1])
                data = bytearray(output.read_bytes())
                struct.pack_into('<H', data, 18, 183)
                output.write_bytes(data)
            return result

        result = unittest.TestResult()
        with mock.patch.object(subprocess, 'run', side_effect=simulated_arm64_compiler):
            unittest.defaultTestLoader.loadTestsFromTestCase(RuntimeArchitectureTests).run(result)
        self.assertTrue(result.wasSuccessful(), str(result.failures + result.errors))


if __name__ == '__main__':
    unittest.main()
