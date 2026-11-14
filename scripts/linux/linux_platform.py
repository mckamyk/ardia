#!/usr/bin/env python3
import json
import pathlib
import platform
import shutil
import struct

ALIASES = {'x64': 'x64', 'x86_64': 'x64', 'amd64': 'x64', 'arm64': 'arm64', 'aarch64': 'arm64'}
MACHINES = {62: 'x64', 183: 'arm64'}


def normalize_cpu(value):
    value = value.removeprefix('linux-')
    if value not in ALIASES:
        raise ValueError(f'unsupported Linux architecture {value!r}')
    return ALIASES[value]


def elf_cpu(path):
    path = pathlib.Path(path)
    size = path.stat().st_size
    with path.open('rb') as handle:
        header = handle.read(64)
        if len(header) != 64 or header[:7] != b'\x7fELF\x02\x01\x01':
            raise ValueError(f'{path}: expected a little-endian ELF64 binary')
        kind, machine, version = struct.unpack_from('<HHI', header, 16)
        phoff = struct.unpack_from('<Q', header, 32)[0]
        ehsize, phsize, phnum = struct.unpack_from('<HHH', header, 52)
        if kind not in (2, 3) or machine not in MACHINES or version != 1 or ehsize != 64 or phsize != 56 or not 0 < phnum < 65535 or phoff < 64 or phoff + phsize * phnum > size:
            raise ValueError(f'{path}: invalid or unsupported ELF header')
        handle.seek(phoff)
        loads = 0
        for _ in range(phnum):
            entry = handle.read(phsize)
            ptype, flags, offset, virtual, physical, filesz, memsz, align = struct.unpack('<IIQQQQQQ', entry)
            if ptype == 1:
                loads += 1
                if filesz > memsz or offset + filesz > size:
                    raise ValueError(f'{path}: invalid ELF load segment')
        if not loads:
            raise ValueError(f'{path}: ELF has no load segments')
    return MACHINES[machine]


def validate_runtime(directory, cpu=None):
    directory = pathlib.Path(directory)
    actual = elf_cpu(directory / 'chrome')
    expected = normalize_cpu(cpu) if cpu is not None else actual
    names = ['chrome', 'chrome_crashpad_handler']
    optional = ('chrome_sandbox', 'libEGL.so', 'libGLESv2.so', 'libvk_swiftshader.so', 'libvulkan.so.1', 'libqt5_shim.so', 'libqt6_shim.so')
    names.extend(name for name in optional if (directory / name).exists() or (directory / name).is_symlink())
    for name in names:
        path = directory / name
        if not path.resolve().is_relative_to(directory.resolve()):
            raise ValueError(f'{path}: runtime binary points outside the engine')
        found = elf_cpu(path)
        if found != expected:
            raise ValueError(f'{path}: ELF architecture is {found}, expected {expected}')
    manifest_path = directory / 'manifest.json'
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        if not isinstance(manifest, dict):
            raise ValueError('the engine manifest must be an object')
        for field, value in (('target_cpu', expected), ('target_os', 'linux')):
            if field in manifest and manifest[field] != value:
                raise ValueError(f'the engine manifest {field} is {manifest[field]!r}, expected {value!r}')
    return expected


def strip_tool(cpu):
    cpu = normalize_cpu(cpu)
    if cpu == normalize_cpu(platform.machine()):
        candidates = ('strip', 'llvm-strip')
    else:
        prefix = {'x64': 'x86_64', 'arm64': 'aarch64'}[cpu]
        candidates = (f'{prefix}-linux-gnu-strip', 'llvm-strip')
    for candidate in candidates:
        tool = shutil.which(candidate)
        if tool:
            return tool
    raise ValueError(f'no strip tool for Linux {cpu}: install ' + ' or '.join(candidates))
