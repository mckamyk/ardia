#!/usr/bin/env python3
import hashlib
import importlib.util
import io
import json
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
MAC_FETCH = 'packages/ardiacore/scripts/fetch-engine.sh'
FIXTURE_FILES = ('engine/engine-key.sh', 'scripts/linux/fetch-engine.sh', 'scripts/linux/runtime_files.py', 'scripts/linux/linux_platform.py', 'scripts/linux/publish-engine.sh',
                 'scripts/linux/publish_engine.py', 'scripts/linux/package.py', 'scripts/agent/locked', MAC_FETCH)
GIT_ENV = {'GIT_CONFIG_GLOBAL': os.devnull, 'GIT_CONFIG_NOSYSTEM': '1', 'GIT_AUTHOR_NAME': 'Test', 'GIT_AUTHOR_EMAIL': 'test@example.com',
           'GIT_COMMITTER_NAME': 'Test', 'GIT_COMMITTER_EMAIL': 'test@example.com'}
ELF = pathlib.Path(shutil.which('true') or '/usr/bin/true')


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


sys.dont_write_bytecode = True
runtime = module('runtime_files', LINUX / 'runtime_files.py')
packaging = module('packaging', LINUX / 'package.py')


def has_build_id(path):
    if not shutil.which('readelf') or not shutil.which('strip') or not path.is_file():
        return False
    notes = subprocess.run(['readelf', '-n', str(path)], capture_output=True, text=True)
    return 'Build ID' in notes.stdout


def parser_elf(cpu):
    data = bytearray(ELF.read_bytes())
    data[18:20] = {'x64': 62, 'arm64': 183}[cpu].to_bytes(2, 'little')
    return bytes(data)


def without_arch_dispatch(source):
    start = source.index('if [ "${1:-}" = --arch ]; then')
    end = source.index('nearest=0\n', start)
    return source[:start] + source[end:]


def native_runtime(directory, junk=True):
    directory.mkdir(parents=True, exist_ok=True)
    for name in runtime.REQUIRED:
        if name == 'locales':
            (directory / name).mkdir(exist_ok=True)
            (directory / name / 'en-US.pak').write_bytes(b'locale')
            (directory / name / 'en-US.pak.info').write_bytes(b'info')
        else:
            (directory / name).write_bytes(b'runtime')
    for name in ('libEGL.so', 'libGLESv2.so', 'snapshot_blob.bin', 'vk_swiftshader_icd.json', 'libvulkan.so.1'):
        (directory / name).write_bytes(b'optional')
    for name in ('chrome', 'chrome_crashpad_handler', 'libEGL.so', 'libGLESv2.so', 'libvulkan.so.1'):
        shutil.copyfile(ELF, directory / name)
    (directory / 'chrome').chmod(0o755)
    if junk:
        for name in ('libEGL.so.TOC', 'libVkICD_mock_icd.so', 'libVkLayer_khronos_validation.so', 'chrome.debug', 'args.gn', 'build.ninja'):
            (directory / name).write_bytes(b'junk')
        (directory / 'obj').mkdir(exist_ok=True)
    return directory


def run(command, cwd=None, env=None, timeout=120):
    return subprocess.run([str(part) for part in command], cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout)


class Fixture:
    def __init__(self, root):
        self.root = root
        self.repo = root / 'repo'
        self.repo.mkdir()
        self.env = {name: value for name, value in os.environ.items() if not name.startswith(('ARDIACORE_', 'AC_LOCK', 'GIT_'))}
        self.env.update(GIT_ENV)
        self.env['AC_LOCK_ROOT'] = str(root / 'locks')
        (root / 'locks').mkdir()
        for relative in FIXTURE_FILES:
            target = self.repo / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(REPO / relative, target)
        (self.repo / 'packages/ardiacore/prebuilt-engines-linux.tsv').write_text('')
        (self.repo / '.gitignore').write_text('apps/browser/build-ardiacore/\n')
        build = self.repo / 'engine/patches/build'
        build.mkdir(parents=True)
        (build / 'env.sh').write_text('export CHROMIUM_VERSION=156.0.1.2\n')
        for game in ('offline-game', 'offline-game-pipeline'):
            (self.repo / 'apps/browser/assets' / game).mkdir(parents=True)
            (self.repo / 'apps/browser/assets' / game / 'version.txt').write_text('one\n')
        self.git('init', '-q', '-b', 'main')
        self.commit_engine('one')

    def git(self, *args, cwd=None):
        result = run(['git', *args], cwd=cwd or self.repo, env=self.env)
        if result.returncode != 0:
            raise AssertionError(result.stderr)
        return result.stdout.strip()

    def commit_engine(self, content):
        (self.repo / 'engine/version.txt').write_text(content + '\n')
        self.git('add', '-A')
        self.git('commit', '-q', '-m', content)
        return self.key()

    def commit_game(self, content):
        (self.repo / 'apps/browser/assets/offline-game/version.txt').write_text(content + '\n')
        self.git('add', '-A')
        self.git('commit', '-q', '-m', content)
        return self.key()

    def commit_other(self, content):
        (self.repo / 'other.txt').write_text(content)
        self.git('add', '-A')
        self.git('commit', '-q', '-m', content)

    def key(self, rev='HEAD'):
        result = run([self.repo / 'engine/engine-key.sh', rev], cwd=self.repo, env=self.env)
        if result.returncode != 0:
            raise AssertionError(result.stderr)
        return result.stdout.strip()

    @property
    def store(self):
        return self.repo / 'apps/browser/build-ardiacore/prebuilt-linux-x86_64'


class ArchiveMaker:
    def __init__(self, root):
        self.assets = root / 'assets'
        self.table = root / 'table.tsv'
        self.table.write_text('')

    def manifest(self, key, **overrides):
        manifest = {'format': 2, 'engine_key': key, 'source_commit': None, 'target_os': 'linux', 'target_cpu': 'x64', 'chrome_sha256': hashlib.sha256(parser_elf('x64')).hexdigest()}
        manifest.update(overrides)
        return json.dumps(manifest).encode()

    def publish(self, key, members=None, manifest=None, sha=None, skip=(), cpu='x64'):
        architecture_fixture = parser_elf(cpu)
        tag = runtime.release_tag(key)
        folder = self.assets / tag
        folder.mkdir(parents=True, exist_ok=True)
        archive = folder / f'ArdiaCore-{key[:12]}-linux-{cpu}.tar.xz'
        with tarfile.open(archive, 'w:xz') as packed:
            def add(name, data=b'', kind=tarfile.REGTYPE, mode=0o644, link=''):
                info = tarfile.TarInfo(name)
                info.type, info.mode, info.linkname = kind, mode, link
                info.size = len(data) if kind == tarfile.REGTYPE else 0
                packed.addfile(info, io.BytesIO(data) if kind == tarfile.REGTYPE else None)
            root = f'ardiacore-linux-{cpu}'
            add(root, kind=tarfile.DIRTYPE, mode=0o755)
            for name in runtime.REQUIRED:
                if name in skip:
                    continue
                if name == 'locales':
                    add(f'{root}/locales', kind=tarfile.DIRTYPE, mode=0o755)
                    add(f'{root}/locales/en-US.pak', b'locale')
                else:
                    add(f'{root}/{name}', architecture_fixture if name in ('chrome', 'chrome_crashpad_handler') else b'runtime', mode=0o755 if name == 'chrome' else 0o644)
            add(f'{root}/manifest.json', manifest if manifest is not None else self.manifest(key, target_cpu=cpu, chrome_sha256=hashlib.sha256(architecture_fixture).hexdigest()))
            for member in members or ():
                add(**member)
        digest = sha or hashlib.sha256(archive.read_bytes()).hexdigest()
        with self.table.open('a') as handle:
            handle.write(f'{key}\t{tag}\t{digest}\n')
        return archive


class ArchiveFixtureTests(unittest.TestCase):
    def test_parser_archives_label_both_cpus_independently_of_source_elf(self):
        original = ELF.read_bytes()
        for source_machine in (62, 183):
            for cpu, machine in (('x64', 62), ('arm64', 183)):
                with self.subTest(source_machine=source_machine, cpu=cpu), tempfile.TemporaryDirectory() as scratch:
                    root = pathlib.Path(scratch)
                    source = root / 'parser-source.elf'
                    data = bytearray(original)
                    data[18:20] = source_machine.to_bytes(2, 'little')
                    source.write_bytes(data)
                    with mock.patch.dict(globals(), ELF=source):
                        archive = ArchiveMaker(root).publish('a' * 40, cpu=cpu)
                    with tarfile.open(archive) as packed:
                        for name in ('chrome', 'chrome_crashpad_handler'):
                            binary = packed.extractfile(f'ardiacore-linux-{cpu}/{name}').read()
                            self.assertEqual(int.from_bytes(binary[18:20], 'little'), machine)


class RuntimeSelectionTests(unittest.TestCase):
    def test_allowlist_keeps_runtime_files_and_drops_build_files(self):
        with tempfile.TemporaryDirectory() as scratch:
            out = native_runtime(pathlib.Path(scratch) / 'out')
            names = runtime.select(out)
            self.assertEqual(names[:len(runtime.REQUIRED)], list(runtime.REQUIRED))
            for name in ('libEGL.so', 'libGLESv2.so', 'snapshot_blob.bin', 'vk_swiftshader_icd.json', 'libvulkan.so.1'):
                self.assertIn(name, names)
            for name in ('libEGL.so.TOC', 'libVkICD_mock_icd.so', 'libVkLayer_khronos_validation.so', 'chrome.debug', 'args.gn', 'obj', 'chrome_sandbox', 'manifest.json'):
                self.assertNotIn(name, names)
            copied = pathlib.Path(scratch) / 'copy'
            runtime.copy(out, copied)
            self.assertEqual(sorted(path.name for path in copied.iterdir()), sorted(names))
            self.assertEqual([path.name for path in (copied / 'locales').iterdir()], ['en-US.pak'])
            shutil.copyfile(ELF, out / 'chrome_sandbox')
            (out / 'manifest.json').write_text('{}')
            self.assertIn('chrome_sandbox', runtime.select(out))
            self.assertIn('manifest.json', runtime.select(out))
            self.assertNotIn(out / 'chrome_sandbox', runtime.strippable(out))

    def test_missing_required_file_fails(self):
        with tempfile.TemporaryDirectory() as scratch:
            out = native_runtime(pathlib.Path(scratch) / 'out')
            (out / 'resources.pak').unlink()
            with self.assertRaisesRegex(ValueError, 'resources.pak'):
                runtime.select(out)
            with self.assertRaisesRegex(ValueError, 'resources.pak'):
                runtime.copy(out, pathlib.Path(scratch) / 'copy')
            with self.assertRaisesRegex(ValueError, 'resources.pak'):
                packaging.package(out, pathlib.Path(scratch), pathlib.Path(scratch) / 'a.tar.xz')
            (out / 'resources.pak').write_bytes(b'runtime')
            (out / 'chrome').chmod(0o644)
            with self.assertRaisesRegex(ValueError, 'executable'):
                runtime.select(out)

    def test_package_accepts_a_fetched_engine_dir(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = pathlib.Path(scratch)
            engine = native_runtime(root / 'engine', junk=False)
            (engine / 'manifest.json').write_text('{"format": 1}')
            (engine / '.complete').write_text('x\n')
            ui = root / 'ui'
            ui.mkdir()
            (ui / 'index.html').write_text('<html></html>')
            archive = packaging.package(engine, ui, root / 'ardia.tar.xz')
            with tarfile.open(archive) as packed:
                names = packed.getnames()
            self.assertIn('ardia/lib/ardia/manifest.json', names)
            self.assertIn('ardia/lib/ardia/chrome', names)
            self.assertIn('ardia/lib/ardia/locales/en-US.pak', names)
            self.assertNotIn('ardia/lib/ardia/.complete', names)
            self.assertFalse([name for name in names if name.endswith(('.TOC', '.info'))])


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('flock') and has_build_id(ELF), 'Linux with flock, strip, readelf and an ELF with a build id')
class PublisherTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.scratch.name)
        self.fixture = Fixture(self.root)
        src = self.root / 'chromium/src'
        self.cpu = runtime.linux_platform.elf_cpu(ELF)
        self.archive_root = f'ardiacore-linux-{self.cpu}'
        self.out = native_runtime(src / f'out/Release_GN_linux_{self.cpu}')
        (self.out / 'args.gn').write_text('target_os="linux"\n')
        (src / 'chrome').mkdir()
        (src / 'chrome/VERSION').write_text('MAJOR=156\nMINOR=0\nBUILD=8078\nPATCH=47\n')

    def tearDown(self):
        self.scratch.cleanup()

    def publish(self, source, dest, *extra):
        return run([self.fixture.repo / 'scripts/linux/publish-engine.sh', '--out', self.out, '--source', source, '--dest', dest, *extra], env=self.fixture.env)

    def snapshot(self, directory):
        return {str(path.relative_to(directory)): (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest())
                for path in directory.rglob('*') if path.is_file()}

    def test_dry_run_from_a_checkout_packs_the_allowlist_with_a_manifest(self):
        before = self.snapshot(self.out)
        dest = self.root / 'dest'
        result = self.publish(self.fixture.repo, dest)
        self.assertEqual(result.returncode, 0, result.stderr)
        key, head = self.fixture.key(), self.fixture.git('rev-parse', 'HEAD')
        archive = dest / f'ArdiaCore-{key[:12]}-linux-{self.cpu}.tar.xz'
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        self.assertEqual(result.stdout.splitlines(), [str(archive), f'{key}\tengine-{key[:12]}\t{digest}'])
        self.assertEqual((dest / (archive.name + '.sha256')).read_text(), f'{digest}  {archive.name}\n')
        self.assertEqual(self.snapshot(self.out), before)
        self.assertTrue((self.root / 'locks/ac-chromium.lockf').exists())
        self.assertFalse((self.root / 'locks/ac-chromium.holder').exists())
        self.assertEqual((self.fixture.repo / 'packages/ardiacore/prebuilt-engines-linux.tsv').read_text(), '')
        with tarfile.open(archive) as packed:
            names = packed.getnames()
            self.assertTrue(all(name == self.archive_root or name.startswith(self.archive_root + '/') for name in names), names)
            top = sorted(name.split('/')[1] for name in names if name.count('/') == 1)
            self.assertEqual(top, sorted(runtime.select(self.out) + ['manifest.json']))
            self.assertNotIn(f'{self.archive_root}/locales/en-US.pak.info', names)
            manifest = json.load(packed.extractfile(f'{self.archive_root}/manifest.json'))
            chrome = packed.extractfile(f'{self.archive_root}/chrome').read()
            self.assertTrue(packed.getmember(f'{self.archive_root}/chrome').mode & 0o111)
            self.assertEqual({member.uid for member in packed.getmembers()}, {0})
        self.assertEqual(manifest['format'], 2)
        self.assertEqual(manifest['engine_key'], key)
        self.assertEqual(manifest['source_commit'], head)
        self.assertEqual((manifest['target_os'], manifest['target_cpu']), ('linux', self.cpu))
        self.assertEqual(manifest['chromium_version'], '156.0.8078.47')
        self.assertEqual(manifest['args_gn_sha256'], hashlib.sha256(b'target_os="linux"\n').hexdigest())
        self.assertEqual(manifest['chrome_sha256'], hashlib.sha256(chrome).hexdigest())
        self.assertRegex(manifest['chrome_build_id'], r'^[0-9a-f]{16,}$')
        self.assertRegex(manifest['built_at'], r'^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$')
        self.assertRegex(manifest['packaged_at'], r'^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$')
        again = self.publish(self.fixture.repo, dest)
        self.assertNotEqual(again.returncode, 0)
        self.assertIn('refusing to overwrite', again.stderr)

    def test_explicit_arch_rejects_mislabeled_native_runtime(self):
        result = self.publish(self.fixture.repo, self.root / 'dest', '--arch', 'arm64' if self.cpu == 'x64' else 'x64')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('architecture', result.stderr)

    def test_dirty_checkout_is_refused(self):
        for index, relative in enumerate(('engine/version.txt', 'apps/browser/assets/offline-game-pipeline/version.txt')):
            with self.subTest(path=relative):
                self.fixture.git('checkout', '-q', '--', '.')
                (self.fixture.repo / relative).write_text('dirty\n')
                dest = self.root / f'dest{index}'
                result = self.publish(self.fixture.repo, dest)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('uncommitted', result.stderr)
                self.assertFalse(dest.exists() and any(dest.iterdir()))

    def test_plain_copy_uses_its_contents_key_and_checks_the_commit_file(self):
        first = self.fixture.git('rev-parse', 'HEAD')
        self.fixture.commit_engine('two')
        copy = self.root / 'builder/repo'
        shutil.copytree(self.fixture.repo, copy, ignore=shutil.ignore_patterns('.git'))
        (copy / 'engine/ignored.log').write_text('ignored')
        for checkout in (copy, self.fixture.repo):
            (checkout / 'engine/.gitignore').write_text('*.log\n')
        self.fixture.git('add', '-A')
        self.fixture.git('commit', '-q', '-m', 'ignore logs')
        key = self.fixture.key()
        head = self.fixture.git('rev-parse', 'HEAD')
        marker = self.root / 'builder/repo.commit'
        cases = [(head, head, ''), (first, None, 'source_commit is null'), ('0' * 40, None, 'source_commit is null')]
        for index, (commit, expected, warning) in enumerate(cases):
            marker.write_text(commit + '\n')
            result = self.publish(copy, self.root / f'dest{index}')
            self.assertEqual(result.returncode, 0, result.stderr)
            with tarfile.open(result.stdout.splitlines()[0]) as packed:
                manifest = json.load(packed.extractfile(f'{self.archive_root}/manifest.json'))
            self.assertEqual(manifest['engine_key'], key)
            self.assertEqual(manifest['source_commit'], expected)
            self.assertIn(warning, result.stderr)
        (copy / 'engine/version.txt').write_text('changed\n')
        marker.write_text(head + '\n')
        result = self.publish(copy, self.root / 'dest-changed')
        self.assertEqual(result.returncode, 0, result.stderr)
        with tarfile.open(result.stdout.splitlines()[0]) as packed:
            manifest = json.load(packed.extractfile(f'{self.archive_root}/manifest.json'))
        self.assertNotEqual(manifest['engine_key'], key)
        self.assertIsNone(manifest['source_commit'])
        (copy / 'engine/version.txt').write_text('two\n')
        (copy / 'apps/browser/assets/offline-game/version.txt').write_text('changed\n')
        result = self.publish(copy, self.root / 'dest-game')
        self.assertEqual(result.returncode, 0, result.stderr)
        with tarfile.open(result.stdout.splitlines()[0]) as packed:
            manifest = json.load(packed.extractfile(f'{self.archive_root}/manifest.json'))
        self.assertNotEqual(manifest['engine_key'], key)
        self.assertIsNone(manifest['source_commit'])
        self.assertEqual(manifest['engine_key'], self.fixture.commit_game('changed'))

    def test_help_and_missing_outputs(self):
        usage = run([self.fixture.repo / 'scripts/linux/publish-engine.sh', '--help'], env=self.fixture.env)
        self.assertEqual(usage.returncode, 0)
        self.assertIn('--upload', usage.stdout)
        (self.out / 'icudtl.dat').unlink()
        result = self.publish(self.fixture.repo, self.root / 'dest')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('icudtl.dat', result.stderr)


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('sha256sum') and shutil.which('curl'), 'Linux with sha256sum and curl')
class FetcherTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.scratch.name)
        self.fixture = Fixture(self.root)
        self.assets = ArchiveMaker(self.root)
        self.env = dict(self.fixture.env, ARDIACORE_PREBUILT_ASSETS=self.assets.assets.as_uri(),
                        ARDIACORE_PREBUILT_LINUX_TABLE=str(self.assets.table), ARDIACORE_PREBUILT_TABLE_URL='none')

    def tearDown(self):
        self.scratch.cleanup()

    def fetch(self, *args, script='scripts/linux/fetch-engine.sh'):
        target = () if '--arch' in args else ('--arch', 'x64')
        return run([self.fixture.repo / script, *target, *args], env=self.env)

    def assert_fetched(self, result, key):
        self.assertEqual(result.returncode, 0, result.stderr)
        engine = self.fixture.store / key[:12]
        self.assertEqual(result.stdout, f'{engine}\n')
        self.assertTrue((engine / '.complete').is_file())
        self.assertEqual(json.loads((engine / 'manifest.json').read_text())['engine_key'], key)
        self.assertTrue(os.access(engine / 'chrome', os.X_OK))
        return engine

    def assert_rejected(self, result, key, message):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)
        self.assertEqual(result.stdout, '')
        self.assertFalse((self.fixture.store / key[:12]).exists())
        self.assertEqual(list(self.fixture.store.glob('.tmp-*')), [])

    def simulate_arm_host(self):
        tools = self.root / 'host-tools'
        tools.mkdir()
        uname = tools / 'uname'
        uname.write_text('#!/bin/sh\nif [ "$1" = -m ]; then echo aarch64; else exec /usr/bin/uname "$@"; fi\n')
        uname.chmod(0o755)
        self.env['PATH'] = str(tools) + os.pathsep + self.env['PATH']

    def test_x64_fetch_fixture_on_simulated_arm_host(self):
        self.simulate_arm_host()
        self.test_exact_hit_and_cache_reuse_without_network()

    def test_simulated_arm_host_defaults_to_arm64(self):
        self.simulate_arm_host()
        key = self.fixture.key()
        self.assets.publish(key, cpu='arm64')
        result = run([self.fixture.repo / 'scripts/linux/fetch-engine.sh'], env=self.env)
        self.assertEqual(result.returncode, 0, result.stderr)
        engine = self.fixture.store.with_name('prebuilt-linux-arm64') / key[:12]
        self.assertEqual(result.stdout.strip(), str(engine))
        self.assertEqual(json.loads((engine / 'manifest.json').read_text())['target_cpu'], 'arm64')

    def test_exact_hit_and_cache_reuse_without_network(self):
        key = self.fixture.key()
        archive = self.assets.publish(key)
        engine = self.assert_fetched(self.fetch(), key)
        archive.unlink()
        again = self.fetch()
        self.assert_fetched(again, key)
        self.assertIn('cached', again.stderr)
        self.assertEqual(sorted(path.name for path in self.fixture.store.iterdir()), ['.lock', key[:12]])
        self.assertEqual((engine / '.complete').read_text().strip(), self.assets.table.read_text().split()[2])

    def test_exact_miss_names_the_key_to_build(self):
        older = self.fixture.key()
        result = self.fetch()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(older[:12], result.stderr)
        self.assertIn('publish-engine.sh --upload', result.stderr)
        self.assets.publish(older)
        key = self.fixture.commit_engine('two')
        result = self.fetch()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(f'engine key {key[:12]}', result.stderr)
        self.assertIn("build this commit's engine", result.stderr)
        self.assertIn(f'--nearest to use {older[:12]}', result.stderr)
        self.assertEqual(result.stdout, '')

    def test_nearest_selects_the_closest_published_engine_with_a_warning(self):
        oldest = self.fixture.key()
        self.assets.publish(oldest)
        older = self.fixture.commit_engine('two')
        self.assets.publish(older)
        self.fixture.commit_engine('three')
        key = self.fixture.commit_engine('four')
        self.fixture.commit_other('docs only')
        result = self.fetch('--nearest')
        self.assert_fetched(result, older)
        self.assertIn(f'no prebuilt Linux engine for engine key {key[:12]}; using {older[:12]}, 2 engine commits older', result.stderr)
        mac = self.fetch('--arch', 'linux-x86_64', '--nearest', script=MAC_FETCH)
        self.assert_fetched(mac, older)

    def test_the_offline_game_is_part_of_the_key(self):
        engine_only = self.fixture.key()
        self.assets.publish(engine_only)
        key = self.fixture.commit_game('two')
        self.assertNotEqual(key, engine_only)
        exact = self.fetch()
        self.assertNotEqual(exact.returncode, 0)
        self.assertIn(f'engine key {key[:12]}', exact.stderr)
        nearest = self.fetch('--nearest')
        self.assert_fetched(nearest, engine_only)
        self.assertIn(f'using {engine_only[:12]}, 1 engine commits older', nearest.stderr)
        self.assets.publish(key)
        self.assert_fetched(self.fetch(), key)
        (self.fixture.repo / 'apps/browser/assets/offline-game/version.txt').write_text('dirty\n')
        dirty = self.fetch()
        self.assertNotEqual(dirty.returncode, 0)
        self.assertIn('apps/browser/assets/offline-game', dirty.stderr)
        self.assertIn('uncommitted', dirty.stderr)

    def test_checksum_mismatch_is_rejected_before_unpacking(self):
        key = self.fixture.key()
        self.assets.publish(key, sha='0' * 64)
        self.assert_rejected(self.fetch(), key, 'SHA-256')

    def test_unsafe_members_are_rejected(self):
        root = runtime.ARCHIVE_ROOT
        cases = [
            {'name': '../escape', 'data': b'x'},
            {'name': f'{root}/../../escape', 'data': b'x'},
            {'name': '/tmp/absolute-escape', 'data': b'x'},
            {'name': 'other/file', 'data': b'x'},
            {'name': f'{root}/link', 'kind': tarfile.SYMTYPE, 'link': '/etc/passwd'},
            {'name': f'{root}/locales/link', 'kind': tarfile.SYMTYPE, 'link': '../../outside'},
            {'name': f'{root}/hard', 'kind': tarfile.LNKTYPE, 'link': 'outside/file'},
            {'name': f'{root}/device', 'kind': tarfile.CHRTYPE},
            {'name': f'{root}/fifo', 'kind': tarfile.FIFOTYPE},
        ]
        for index, member in enumerate(cases):
            with self.subTest(member=member['name']):
                key = self.fixture.commit_engine(f'unsafe {index}')
                self.assets.publish(key, members=[member])
                self.assert_rejected(self.fetch(), key, 'isn\'t a valid Linux engine')
                self.assertFalse((self.root / 'escape').exists())
                self.assertFalse(pathlib.Path('/tmp/absolute-escape').exists())

    def test_inside_links_are_allowed(self):
        key = self.fixture.key()
        self.assets.publish(key, members=[{'name': f'{runtime.ARCHIVE_ROOT}/locales/default.pak', 'kind': tarfile.SYMTYPE, 'link': 'en-US.pak'}])
        engine = self.assert_fetched(self.fetch(), key)
        self.assertEqual((engine / 'locales/default.pak').read_bytes(), b'locale')

    def test_manifest_and_required_files_are_checked(self):
        cases = [({'manifest': self.assets.manifest('f' * 40)}, 'engine_key'), ({'skip': ('resources.pak',)}, 'resources.pak')]
        for index, (options, message) in enumerate(cases):
            with self.subTest(message=message):
                key = self.fixture.commit_engine(f'manifest {index}')
                self.assets.publish(key, **options)
                self.assert_rejected(self.fetch(), key, message)
        key = self.fixture.commit_engine('wrong target')
        self.assets.publish(key, manifest=self.assets.manifest(key, target_cpu='arm64'))
        self.assert_rejected(self.fetch(), key, 'target_cpu')

    def test_dirty_engine_is_refused_and_old_engines_are_pruned(self):
        keys = [self.fixture.key()]
        commits = [self.fixture.git('rev-parse', 'HEAD')]
        for name in ('two', 'three'):
            keys.append(self.fixture.commit_engine(name))
            commits.append(self.fixture.git('rev-parse', 'HEAD'))
        for key in keys:
            self.assets.publish(key)
        for commit, key in zip(commits, keys):
            self.fixture.git('checkout', '-q', commit)
            self.assert_fetched(self.fetch(), key)
        kept = sorted(path.name for path in self.fixture.store.iterdir() if path.is_dir())
        self.assertEqual(kept, sorted(key[:12] for key in keys[1:]))
        (self.fixture.repo / 'engine/version.txt').write_text('dirty\n')
        result = self.fetch()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('uncommitted', result.stderr)

    def test_explicit_x64_arch_and_wrapper_alias(self):
        key = self.fixture.key()
        self.assets.publish(key)
        self.assert_fetched(self.fetch('--arch', 'x64'), key)
        self.assert_fetched(self.fetch('--arch', 'linux-x64', script=MAC_FETCH), key)

    def test_corrupt_cache_is_not_reused_without_table(self):
        key = self.fixture.key()
        self.assets.publish(key)
        engine = self.assert_fetched(self.fetch(), key)
        (engine / 'chrome').write_bytes(b'corrupt')
        self.assets.table.write_text('')
        result = self.fetch()
        self.assertNotEqual(result.returncode, 0)

    def test_arm64_archive_uses_separate_table_and_cache(self):
        key = self.fixture.key()
        self.assets.publish(key, cpu='arm64')
        table = self.fixture.repo / 'packages/ardiacore/prebuilt-engines-linux-arm64.tsv'
        table.write_text(self.assets.table.read_text())
        self.env.pop('ARDIACORE_PREBUILT_LINUX_TABLE')
        result = self.fetch('--arch', 'linux-aarch64', script=MAC_FETCH)
        self.assertEqual(result.returncode, 0, result.stderr)
        engine = self.fixture.store.with_name('prebuilt-linux-arm64') / key[:12]
        self.assertEqual(result.stdout.strip(), str(engine))
        self.assertEqual(json.loads((engine / 'manifest.json').read_text())['target_cpu'], 'arm64')
        self.assertNotEqual(self.fetch('--arch', 'x64').returncode, 0)
        self.assertFalse(self.fixture.store.exists())
        again = self.fetch('--arch', 'arm64')
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertIn('cached', again.stderr)

    def test_mislabeled_arm64_archive_rejects_x64_elf(self):
        key = self.fixture.key()
        self.assets.publish(key, cpu='arm64', members=[{'name': 'ardiacore-linux-arm64/chrome', 'data': parser_elf('x64'), 'mode': 0o755}],
                            manifest=self.assets.manifest(key, target_cpu='arm64'))
        result = self.fetch('--arch', 'arm64')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('architecture', result.stderr)
        self.assertEqual(result.stdout, '')

    def test_usage(self):
        self.assertEqual(self.fetch('--help').returncode, 0)
        self.assertNotEqual(self.fetch('--bogus').returncode, 0)
        self.assertNotEqual(self.fetch('--nearest', 'extra').returncode, 0)


class MacDispatchTests(unittest.TestCase):
    def test_only_the_arch_dispatch_was_added(self):
        current = (REPO / MAC_FETCH).read_text()
        dispatch = current[current.index('if [ "${1:-}" = --arch ]; then'):current.index('nearest=0\n')]
        self.assertIn('linux-arm64', dispatch)
        committed = subprocess.run(['git', 'show', f'HEAD:{MAC_FETCH}'], cwd=REPO, capture_output=True, text=True, check=True).stdout
        self.assertEqual(without_arch_dispatch(current), without_arch_dispatch(committed))
        self.assertLess(current.index(dispatch), current.index('nearest=0\n'))

    def test_dispatch_comparison_after_current_change_is_committed(self):
        current = (REPO / MAC_FETCH).read_text()
        committed = subprocess.CompletedProcess(['git', 'show'], 0, stdout=current)
        with mock.patch.object(subprocess, 'run', return_value=committed):
            self.test_only_the_arch_dispatch_was_added()

    def test_unknown_arch_fails_and_mac_usage_is_unchanged(self):
        script = REPO / MAC_FETCH
        bogus = run([script, '--arch', 'bogus'])
        self.assertEqual(bogus.returncode, 1)
        self.assertIn('--arch linux-x86_64', bogus.stderr)
        self.assertNotEqual(run([script, '--arch']).returncode, 0)
        mac = run([script, '--bogus'])
        self.assertEqual((mac.returncode, mac.stderr), (1, 'error: usage: fetch-engine.sh [--nearest]\n'))


if __name__ == '__main__':
    unittest.main()
