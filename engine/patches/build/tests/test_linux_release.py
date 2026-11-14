#!/usr/bin/env python3
import hashlib
import importlib.util
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

REPO = pathlib.Path(__file__).resolve().parents[4]
LINUX = REPO / 'scripts/linux'
VERSION = '0.2.44-nightly.3'


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


sys.dont_write_bytecode = True
packaging = module('packaging', LINUX / 'package.py')


def built_elf_with_fixture_machine(cpu="x64"):
    with tempfile.TemporaryDirectory() as scratch:
        target = pathlib.Path(scratch) / "fixture"
        subprocess.run(["cc", "-x", "c", "-", "-o", str(target)], input="int main(void) { return 0; }", text=True, check=True, capture_output=True)
        data = bytearray(target.read_bytes())
        data[18:20] = {"x64": 62, "arm64": 183}[cpu].to_bytes(2, "little")
        return bytes(data)


def fake_engine(root, cpu="x64"):
    output, ui = root / 'out', root / 'ui'
    output.mkdir()
    ui.mkdir()
    for name in packaging.REQUIRED:
        if name == 'locales':
            (output / name).mkdir()
            (output / name / 'en-US.pak').write_bytes(b'locale')
        else:
            (output / name).write_bytes(b'runtime')
    for name in ('chrome', 'chrome_crashpad_handler'):
        (output / name).write_bytes(built_elf_with_fixture_machine(cpu))
        (output / name).chmod(0o755)
    (ui / 'index.html').write_text('<html></html>')
    return output, ui


def fake_cli(directory, version=VERSION, platform='linux-x64', top=None, sidecar=True, cpu='x64', extra=None, python_link='python3.13', esbuild_path='python/lib/python3.13/site-packages/ardia_cli/bin/esbuild'):
    directory.mkdir(parents=True, exist_ok=True)
    name = f'ardia-cli-{version}-{platform}.tar.gz'
    top = top or f'ardia-cli-{version}'
    tarball = directory / name
    with tarfile.open(tarball, 'w:gz') as packed:
        for member, body in [(f'{top}/bin/ardia', b'#!/bin/sh\necho ardia\n'), (f'{top}/python/bin/python3.13', built_elf_with_fixture_machine(cpu)), (f'{top}/{esbuild_path}', built_elf_with_fixture_machine(cpu))] + ([extra] if extra else []):
            info = tarfile.TarInfo(member)
            info.size = len(body)
            info.mode = 0o755
            packed.addfile(info, io.BytesIO(body))
        link = tarfile.TarInfo(f'{top}/python/bin/python3')
        link.type = tarfile.SYMTYPE
        link.linkname = python_link
        packed.addfile(link)
    if sidecar:
        digest = hashlib.sha256(tarball.read_bytes()).hexdigest()
        (directory / (name + '.sha256')).write_text(f'{digest}  {name}\n')
    return tarball


class FixtureArchitectureTests(unittest.TestCase):
    def test_parser_fixture_cpu_is_explicit_on_either_compiler_host(self):
        native = bytearray(built_elf_with_fixture_machine())
        for compiler_machine in (62, 183):
            native[18:20] = compiler_machine.to_bytes(2, 'little')
            for cpu, expected in (('x64', 62), ('arm64', 183)):
                with self.subTest(compiler_machine=compiler_machine, cpu=cpu), mock.patch.object(pathlib.Path, 'read_bytes', return_value=bytes(native)):
                    fixture = built_elf_with_fixture_machine(cpu)
                    self.assertEqual(int.from_bytes(fixture[18:20], 'little'), expected)


class PackageVersionAndCliTests(unittest.TestCase):
    def test_arm64_package_requires_matching_cli_elf(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            output, ui = fake_engine(root, 'arm64')
            cli = fake_cli(root / 'cli', platform='linux-arm64', cpu='arm64')
            self.assertTrue(packaging.package(output, ui, root / 'arm.tar.xz', version=VERSION, cli=cli, cpu='arm64').is_file())
            wrong = fake_cli(root / 'wrong', platform='linux-arm64')
            with self.assertRaisesRegex(ValueError, 'architecture|CPU|cpu'):
                packaging.package(output, ui, root / 'wrong.tar.xz', version=VERSION, cli=wrong, cpu='arm64')
            with self.assertRaises(ValueError):
                packaging.package(output, ui, root / 'mislabeled.tar.xz', cpu='x64')

    def test_cli_validates_actual_python_target(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            cli = fake_cli(root, platform='linux-arm64', cpu='arm64', python_link='python', extra=(f'ardia-cli-{VERSION}/python/bin/python', built_elf_with_fixture_machine('x64')))
            with self.assertRaisesRegex(ValueError, 'architecture'):
                packaging.check_cli(cli, VERSION, cpu='arm64')

    def test_cli_rejects_duplicate_entries_and_fake_esbuild_location(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            duplicate = fake_cli(root / 'duplicate', extra=(f'ardia-cli-{VERSION}/bin/ardia', b'bad'))
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                packaging.check_cli(duplicate, VERSION)
            misplaced = fake_cli(root / 'misplaced', esbuild_path='decoy/ardia_cli/bin/esbuild')
            with self.assertRaisesRegex(ValueError, 'esbuild'):
                packaging.check_cli(misplaced, VERSION)

    def test_cli_rejects_missing_and_cyclic_python_links(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            for target in ('missing', 'python3'):
                cli = fake_cli(root / target, python_link=target)
                with self.assertRaises(ValueError):
                    packaging.check_cli(cli, VERSION)

    def test_cli_rejects_incompatible_native_extension(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            cli = fake_cli(root, extra=(f'ardia-cli-{VERSION}/python/lib/python3.13/site-packages/native.so', built_elf_with_fixture_machine('arm64')))
            with self.assertRaisesRegex(ValueError, 'architecture'):
                packaging.check_cli(cli, VERSION)

    def test_cli_rejects_traversal(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            cli = fake_cli(root, extra=(f'ardia-cli-{VERSION}/../escape', b'bad'))
            with self.assertRaisesRegex(ValueError, 'unsafe'):
                packaging.check_cli(cli, VERSION)

    def test_release_package_carries_version_cli_installer_and_digest(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            output, ui = fake_engine(root)
            tarball = fake_cli(root / 'cli')
            archive = packaging.package(output, ui, root / 'ardia.tar.xz', version=VERSION, cli=tarball)
            with tarfile.open(archive) as packed:
                version = packed.getmember('ardia/lib/ardia/VERSION')
                self.assertEqual(packed.extractfile(version).read().decode(), VERSION + '\n')
                self.assertEqual(version.mode & 0o777, 0o644)
                installer = packed.getmember('ardia/lib/ardia/cli/ardia-cli-install.sh')
                self.assertEqual(installer.mode & 0o777, 0o755)
                self.assertEqual(packed.extractfile(installer).read(), (REPO / 'apps/cli/install.sh').read_bytes())
                shipped = packed.getmember(f'ardia/lib/ardia/cli/{tarball.name}')
                self.assertEqual(shipped.mode & 0o777, 0o644)
                self.assertEqual(packed.extractfile(shipped).read(), tarball.read_bytes())
                digest = packed.extractfile('ardia/lib/ardia/cli/ardia-cli.sha256').read().decode().split()
                self.assertEqual(digest, [hashlib.sha256(tarball.read_bytes()).hexdigest(), tarball.name])
                self.assertEqual(packed.getmember('ardia/lib/ardia/cli').mode & 0o777, 0o755)

    def test_dev_package_has_neither_version_nor_cli(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            output, ui = fake_engine(root)
            archive = packaging.package(output, ui, root / 'dev.tar.xz')
            with tarfile.open(archive) as packed:
                names = packed.getnames()
            self.assertNotIn('ardia/lib/ardia/VERSION', names)
            self.assertFalse(any(name.startswith('ardia/lib/ardia/cli') for name in names))
            archive = packaging.package(output, ui, root / 'versioned.tar.xz', version='1.2.3')
            with tarfile.open(archive) as packed:
                self.assertEqual(packed.extractfile('ardia/lib/ardia/VERSION').read(), b'1.2.3\n')
                self.assertFalse(any(name.startswith('ardia/lib/ardia/cli') for name in packed.getnames()))

    def test_versions_follow_the_contract(self):
        for good in ('0.2.44', '0.2.44-nightly.3', '10.20.30-rc.1'):
            self.assertEqual(packaging.check_version(good), good)
        for bad in ('0.2', 'v0.2.44', '0.2.44-nightly', '0.2.44-Nightly.1', '0.2.44-nightly.1.2', '0.2.44\n', '', None):
            with self.assertRaises(ValueError):
                packaging.check_version(bad)

    def test_cli_tarball_must_match_version_platform_digest_and_layout(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            output, ui = fake_engine(root)
            cases = {
                'other version': (fake_cli(root / 'a', version='0.2.43'), 'not ardia-cli-0.2.44-nightly.3-linux-x64'),
                'mac tarball': (fake_cli(root / 'b', platform='macos-arm64'), 'linux-x64'),
                'stray top': (fake_cli(root / 'c', top='other'), 'unsafe archive entry'),
            }
            for label, (tarball, message) in cases.items():
                with self.subTest(label), self.assertRaisesRegex(ValueError, message):
                    packaging.package(output, ui, root / f'{label}.tar.xz', version=VERSION, cli=tarball)
            with self.assertRaisesRegex(ValueError, 'needs --version'):
                packaging.package(output, ui, root / 'unversioned.tar.xz', cli=fake_cli(root / 'd'))
            tampered = fake_cli(root / 'e')
            (root / 'e' / (tampered.name + '.sha256')).write_text('0' * 64 + '  ' + tampered.name + '\n')
            with self.assertRaisesRegex(ValueError, 'says'):
                packaging.package(output, ui, root / 'tampered.tar.xz', version=VERSION, cli=tampered)
            unchecked = fake_cli(root / 'f', sidecar=False)
            self.assertTrue(packaging.package(output, ui, root / 'unchecked.tar.xz', version=VERSION, cli=unchecked).is_file())
            self.assertFalse((root / 'other version.tar.xz').exists())

    def test_command_line_requires_version_with_cli(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            output, ui = fake_engine(root)
            tarball = fake_cli(root / 'cli')
            base = [sys.executable, '-I', str(LINUX / 'package.py'), '--out', str(output), '--ui', str(ui)]
            refused = subprocess.run(base + ['--destination', str(root / 'a.tar.xz'), '--cli', str(tarball)], capture_output=True, text=True, timeout=60)
            self.assertEqual(refused.returncode, 2)
            self.assertIn('--cli needs --version', refused.stderr)
            bad = subprocess.run(base + ['--destination', str(root / 'b.tar.xz'), '--version', '1.2'], capture_output=True, text=True, timeout=60)
            self.assertEqual(bad.returncode, 2)
            self.assertIn('bad version', bad.stderr)
            made = subprocess.run(base + ['--destination', str(root / 'c.tar.xz'), '--version', VERSION, '--cli', str(tarball)], capture_output=True, text=True, timeout=120)
            self.assertEqual(made.returncode, 0, made.stderr)
            self.assertTrue((root / 'c.tar.xz').is_file())

    def test_system_install_carries_version_and_cli(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            output, ui = fake_engine(root)
            tarball = fake_cli(root / 'cli')
            archive = packaging.package(output, ui, root / 'ardia.tar.xz', version=VERSION, cli=tarball)
            with tarfile.open(archive) as packed:
                packed.extractall(root / 'extracted', filter='tar')
            result = subprocess.run([str(root / 'extracted/ardia/install.sh'), '--prefix', '/opt/ardia', '--destdir', str(root / 'stage')],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            installed = root / 'stage/opt/ardia/lib/ardia'
            self.assertEqual((installed / 'VERSION').read_text(), VERSION + '\n')
            self.assertEqual((installed / 'cli/ardia-cli-install.sh').stat().st_mode & 0o777, 0o755)
            self.assertEqual((installed / f'cli/{tarball.name}').read_bytes(), tarball.read_bytes())
            self.assertTrue((installed / 'cli/ardia-cli.sha256').is_file())


class ArchPackageTests(unittest.TestCase):
    def pkgver(self, srcdir):
        script = f'source {str(LINUX / "arch/PKGBUILD")!r}; srcdir={str(srcdir)!r}; pkgver'
        result = subprocess.run(['bash', '-c', script], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_architecture_sources(self):
        result = subprocess.run(['bash', '-c', f'source {LINUX / "arch/PKGBUILD"}; declare -p arch source_x86_64 source_aarch64'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('aarch64', result.stdout)
        self.assertIn('ardia-linux-arm64.tar.xz', result.stdout)

    def test_pkgver_comes_from_the_package_version(self):
        with tempfile.TemporaryDirectory() as scratch:
            srcdir = pathlib.Path(scratch)
            self.assertEqual(self.pkgver(srcdir), '0.0.0')
            (srcdir / 'ardia/lib/ardia').mkdir(parents=True)
            (srcdir / 'ardia/lib/ardia/VERSION').write_text(VERSION + '\n')
            self.assertEqual(self.pkgver(srcdir), '0.2.44_nightly.3')
            (srcdir / 'ardia/lib/ardia/VERSION').write_text('0.2.45\n')
            self.assertEqual(self.pkgver(srcdir), '0.2.45')


class ReleaseScriptTests(unittest.TestCase):
    def run_release(self, *args):
        return subprocess.run(['bash', str(LINUX / 'release.sh'), *args], capture_output=True, text=True, timeout=60, cwd=REPO)

    def test_target_argument(self):
        result = self.run_release('99.0.0', '--arch', 'arm64')
        self.assertTrue('MARKETING_VERSION' in result.stderr or 'requires a native arm64 host' in result.stderr, result.stderr)
        self.assertIn('--arch', self.run_release('--help').stdout)

    def test_smoke_cli_architecture(self):
        source = f"import {{cliTarball,packageStagedFiles}} from '{(LINUX / 'smoke.mjs').as_uri()}'; console.log(cliTarball('1.2.3','arm64')); console.log(JSON.stringify(packageStagedFiles({{version:'1.2.3',cli:true,cpu:'arm64'}})));"
        result = subprocess.run(['node', '--input-type=module', '-e', source], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('linux-x64', result.stdout)
        self.assertIn('linux-arm64', result.stdout)

    def test_help_and_argument_errors(self):
        usage = self.run_release('--help')
        self.assertEqual(usage.returncode, 0)
        for text in ('--engine exact', '--allow-dirty', 'gh release upload', 'dist/<version>/linux'):
            self.assertIn(text, usage.stdout)
        self.assertEqual(self.run_release().returncode, 64)
        self.assertEqual(self.run_release('0.2.44', '--bogus').returncode, 64)
        self.assertIn('neither a stable', self.run_release('0.2.44-rc.1').stderr)
        self.assertIn('MARKETING_VERSION', self.run_release('99.0.0').stderr)
        self.assertIn('needs exact, nearest or a directory', self.run_release('99.0.0', '--engine').stderr)

    def test_script_is_valid_bash_without_comments(self):
        source = (LINUX / 'release.sh').read_text()
        self.assertEqual(subprocess.run(['bash', '-n', str(LINUX / 'release.sh')]).returncode, 0)
        lines = [line.strip() for line in source.splitlines()[1:]]
        self.assertFalse([line for line in lines if line.startswith('#')])
        self.assertTrue(os.access(LINUX / 'release.sh', os.X_OK))


if __name__ == '__main__':
    unittest.main()
