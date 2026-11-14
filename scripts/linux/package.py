#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import pathlib
import posixpath
import re
import shutil
import sys
import tarfile
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import runtime_files
import linux_platform

REPO = pathlib.Path(__file__).resolve().parents[2]
BRIDGE = REPO / 'engine/ardiacore-linux/content-blocker'
BRIDGE_SCRIPTS = ('js/vapi.js', 'js/vapi-common.js', 'js/vapi-client.js')
CLI_INSTALLER = REPO / 'apps/cli/install.sh'
VERSION_PATTERN = re.compile(r'^[0-9]+\.[0-9]+\.[0-9]+(-[a-z]+\.[0-9]+)?$')
REQUIRED = runtime_files.REQUIRED


def check_content_blocker(source):
    manifest_path = source / 'manifest.json'
    if not manifest_path.is_file():
        raise ValueError(f'no uBlock Origin manifest at {manifest_path}')
    manifest = json.loads(manifest_path.read_text())
    if not isinstance(manifest.get('key'), str) or manifest.get('manifest_version') != 2:
        raise ValueError('the content blocker is not the keyed uBlock Origin build from scripts/linux/fetch-ubo.sh')
    missing = [name for name in BRIDGE_SCRIPTS if not (source / name).is_file()]
    if missing:
        raise ValueError('the content blocker lacks ' + ', '.join(missing))


def check_version(version):
    if not isinstance(version, str) or not VERSION_PATTERN.fullmatch(version):
        raise ValueError(f'bad version {version!r}: expected <major>.<minor>.<patch> or <major>.<minor>.<patch>-<channel>.<n>')
    return version


def cli_tarball_name(version, cpu="x64"):
    return f'ardia-cli-{version}-linux-{linux_platform.normalize_cpu(cpu)}.tar.gz'


def sha256_of(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def check_cli(tarball, version, installer=CLI_INSTALLER, cpu="x64"):
    cpu = linux_platform.normalize_cpu(cpu)
    expected = cli_tarball_name(version, cpu)
    if tarball.name != expected:
        raise ValueError(f'the CLI tarball is {tarball.name}, not {expected} (the package version and linux-{cpu})')
    if not tarball.is_file():
        raise ValueError(f'no CLI tarball at {tarball}')
    if not installer.is_file():
        raise ValueError(f'no CLI installer at {installer}')
    digest = sha256_of(tarball)
    sidecar = tarball.with_name(tarball.name + '.sha256')
    if sidecar.exists():
        recorded = (sidecar.read_text().split() or [''])[0]
        if recorded != digest:
            raise ValueError(f'{tarball.name} has sha256 {digest}, but {sidecar.name} says {recorded!r}')
    top = f'ardia-cli-{version}'
    try:
        with tarfile.open(tarball, 'r:gz') as packed:
            names = packed.getnames()
            members = {}
            for member in packed.getmembers():
                runtime_files.check_member(member, top)
                name = posixpath.normpath(member.name)
                if name in members:
                    raise ValueError(f'duplicate CLI archive entry {name!r}')
                members[name] = member
            for name in members:
                for parent in pathlib.PurePosixPath(name).parents:
                    ancestor = members.get(str(parent))
                    if ancestor is not None and not ancestor.isdir():
                        raise ValueError(f'unsafe CLI archive parent {str(parent)!r}')

            def executable_member(name):
                seen = set()
                while name not in seen:
                    seen.add(name)
                    member = members.get(name)
                    if member is None:
                        raise ValueError(f'CLI executable target is missing: {name}')
                    if member.isfile():
                        return member
                    if member.issym() or member.islnk():
                        anchor = posixpath.dirname(name) if member.issym() else ''
                        name = posixpath.normpath(posixpath.join(anchor, member.linkname))
                    else:
                        raise ValueError(f'CLI executable is not a file: {name}')
                raise ValueError(f'cyclic CLI executable link: {name}')

            python = executable_member(f'{top}/python/bin/python3')
            esbuild_pattern = re.compile(re.escape(top) + r'/python/lib/python[0-9]+\.[0-9]+/site-packages/ardia_cli/bin/esbuild')
            esbuild = [executable_member(name) for name in members if esbuild_pattern.fullmatch(name)]
            if not esbuild:
                raise ValueError('the CLI archive lacks its esbuild executable in Python site-packages')
            binaries = [python, *esbuild]
            binaries.extend(member for name, member in members.items() if member.isfile() and (name.startswith(f'{top}/python/bin/python3') or '.so' in pathlib.PurePosixPath(name).suffixes))
            with tempfile.TemporaryDirectory(prefix='ardia-cli-check-') as scratch:
                for member in binaries:
                    executable = pathlib.Path(scratch) / 'executable'
                    with packed.extractfile(member) as source, executable.open('wb') as target:
                        shutil.copyfileobj(source, target)
                    actual = linux_platform.elf_cpu(executable)
                    if actual != cpu:
                        raise ValueError(f'CLI architecture {actual} does not match {cpu}: {member.name}')
    except (tarfile.TarError, OSError, EOFError) as error:
        raise ValueError(f'{tarball.name} is not a gzipped tarball: {error}') from None
    stray = [name for name in names if name.rstrip('/') != top and not name.startswith(top + '/')]
    if stray:
        raise ValueError(f'{tarball.name} has entries outside {top}/: ' + ', '.join(stray[:3]))
    if f'{top}/bin/ardia' not in names:
        raise ValueError(f'{tarball.name} has no {top}/bin/ardia')
    return digest


def add_cli(runtime, tarball, digest, installer=CLI_INSTALLER):
    cli = runtime / 'cli'
    cli.mkdir()
    cli.chmod(0o755)
    shutil.copyfile(tarball, cli / tarball.name)
    (cli / tarball.name).chmod(0o644)
    shutil.copyfile(installer, cli / 'ardia-cli-install.sh')
    (cli / 'ardia-cli-install.sh').chmod(0o755)
    (cli / 'ardia-cli.sha256').write_text(f'{digest}  {tarball.name}\n')
    (cli / 'ardia-cli.sha256').chmod(0o644)
    return digest


def package(output, ui, destination, content_blocker=None, version=None, cli=None, cpu=None):
    if version is not None:
        check_version(version)
    cpu = linux_platform.validate_runtime(output, cpu)
    cli_digest = None
    if cli is not None:
        if version is None:
            raise ValueError('a package with the CLI needs --version')
        cli_digest = check_cli(cli, version, cpu=cpu)
    missing = runtime_files.missing(output)
    if missing:
        raise ValueError('missing Linux build outputs: ' + ', '.join(missing))
    if not (ui / 'index.html').is_file():
        raise ValueError(f'no UI bundle at {ui}/index.html')
    if not os.access(output / 'chrome', os.X_OK):
        raise ValueError('chrome is not executable')
    if destination.exists():
        raise ValueError(f'refusing to overwrite {destination}')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ardia-package-') as scratch:
        root = pathlib.Path(scratch) / 'ardia'
        runtime = root / 'lib/ardia'
        runtime.mkdir(parents=True)
        runtime_files.copy(output, runtime, cpu)
        shutil.copytree(ui, runtime / 'ardia-ui')
        if version is not None:
            (runtime / 'VERSION').write_text(version + '\n')
            (runtime / 'VERSION').chmod(0o644)
        if cli is not None:
            add_cli(runtime, cli, cli_digest)
        if content_blocker is not None:
            check_content_blocker(content_blocker)
            blocker = runtime / 'extensions/ublock'
            shutil.copytree(content_blocker, blocker, ignore=shutil.ignore_patterns('_metadata'))
            for path in sorted(BRIDGE.iterdir()):
                shutil.copy2(path, blocker / path.name)
        launcher = root / 'bin/ardia'
        launcher.parent.mkdir()
        launcher.write_text('#!/usr/bin/env sh\nardia_bin_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec "$ardia_bin_dir/../lib/ardia/chrome" "$@"\n')
        launcher.chmod(0o755)
        desktop = root / 'share/applications/ardia.desktop'
        desktop.parent.mkdir(parents=True)
        desktop.write_text('[Desktop Entry]\nType=Application\nName=Ardia\nExec=ardia %U\nIcon=ardia\nTerminal=false\nCategories=Network;WebBrowser;\nMimeType=text/html;x-scheme-handler/http;x-scheme-handler/https;\nStartupWMClass=ardia\n')
        policy = root / 'share/polkit-1/actions/dev.ardia.linux.policy'
        policy.parent.mkdir(parents=True)
        shutil.copyfile(pathlib.Path(__file__).resolve().parent / 'dev.ardia.linux.policy', policy)
        installer = root / 'install.sh'
        shutil.copyfile(pathlib.Path(__file__).resolve().parent / 'install.sh', installer)
        installer.chmod(0o755)
        icon = pathlib.Path(__file__).resolve().parents[2] / 'engine/chromium/src/chrome/app/theme/chromium/product_logo_256.png'
        if icon.is_file():
            target = root / 'share/icons/hicolor/256x256/apps/ardia.png'
            target.parent.mkdir(parents=True)
            shutil.copyfile(icon, target)
        with tarfile.open(destination, 'x:xz') as archive:
            archive.add(root, arcname='ardia')
    return destination


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--ui', type=pathlib.Path, required=True)
    parser.add_argument('--destination', type=pathlib.Path, required=True)
    parser.add_argument('--content-blocker', type=pathlib.Path)
    parser.add_argument('--version', help='the Ardia version, written to lib/ardia/VERSION')
    parser.add_argument('--arch', choices=('x64', 'arm64'))
    parser.add_argument('--cli', type=pathlib.Path, help='the matching ardia-cli-<version>-linux-<arch>.tar.gz (needs --version)')
    args = parser.parse_args()
    if args.cli is not None and args.version is None:
        parser.error('--cli needs --version')
    try:
        blocker = args.content_blocker.resolve() if args.content_blocker else None
        cli = args.cli.resolve() if args.cli else None
        print(package(args.out.resolve(), args.ui.resolve(), args.destination.resolve(), blocker, args.version, cli, args.arch))
    except (ValueError, OSError, json.JSONDecodeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
