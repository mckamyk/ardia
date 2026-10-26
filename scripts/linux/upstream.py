#!/usr/bin/env python3
import argparse
import configparser
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
UPSTREAM = 'https://github.com/mantrakp04/ardia.git'


def git(*args, check=True):
    result = subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True)
    if check and result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result


def content(ref, path):
    return git('show', f'{ref}:{path}', check=False).stdout


def engine_identity(env, pins):
    chromium = re.search(r'^export CHROMIUM_VERSION=(\S+)', env, re.M)
    config = configparser.ConfigParser(interpolation=None)
    config.read_string(pins)
    selected = tuple((section, tuple(sorted(config[section].items()))) for section in ('chromium', 'helium-chromium', 'helium-linux') if section in config)
    return chromium.group(1) if chromium else None, selected


def classify(paths, old_env, new_env, old_pins, new_pins):
    full = engine_identity(old_env, old_pins) != engine_identity(new_env, new_pins)
    engine = any(path.startswith(('engine/chromium/', 'engine/ardiacore-linux/', 'engine/patches/', 'scripts/linux/')) for path in paths)
    ui = any(path.startswith(('apps/browser/src/', 'apps/linux/', 'packages/')) and '/ios/' not in path for path in paths)
    ui = ui or any(path in ('package.json', 'pnpm-lock.yaml', 'pnpm-workspace.yaml', 'turbo.json') or path.startswith('patches/') for path in paths)
    return 'ENGINE_FULL' if full else 'ENGINE_INCREMENTAL' if engine else 'UI_ONLY' if ui else 'NONE'


def status(ref):
    head = git('rev-parse', '--verify', f'{ref}^{{commit}}').stdout.strip()
    base = git('merge-base', 'HEAD', head).stdout.strip()
    engine_tree = git('rev-parse', 'HEAD:engine').stdout.strip()[:12]
    paths = git('diff', '--name-only', '-z', base, head).stdout.rstrip('\0').split('\0') if base != head else []
    level = classify(paths, content(base, 'engine/patches/build/env.sh'), content(head, 'engine/patches/build/env.sh'), content(base, 'engine/patches/build/helium-pins.ini'), content(head, 'engine/patches/build/helium-pins.ini'))
    return {'up_to_date': base == head, 'commits_behind': int(git('rev-list', '--count', f'{base}..{head}').stdout),
            'rebuild_level': level, 'abi_or_gn_changed': any(path.endswith(('public/ac_engine.h', 'BUILD.gn')) for path in paths),
            'upstream_head': head, 'base': base, 'changed_files': paths, 'engine_tree': engine_tree}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=('check', 'sync'))
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--no-fetch', action='store_true')
    parser.add_argument('--ref', default='upstream/main')
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    try:
        if args.command == 'sync' and args.apply:
            if git('status', '--porcelain').stdout:
                raise RuntimeError('sync requires a clean checkout; preserve uncommitted work before rebasing')
            if not git('symbolic-ref', '--quiet', '--short', 'HEAD', check=False).stdout.strip():
                raise RuntimeError('sync requires a branch, not detached HEAD')
        if not args.no_fetch:
            remote = git('remote', 'get-url', 'upstream', check=False)
            if remote.returncode:
                git('remote', 'add', 'upstream', UPSTREAM)
            elif remote.stdout.strip().removesuffix('.git') not in (UPSTREAM.removesuffix('.git'), 'git@github.com:mantrakp04/ardia'):
                raise RuntimeError('upstream points to another repository; refusing to replace or fetch it')
            git('fetch', '--quiet', 'upstream', 'main')
        info = status(args.ref)
        print(json.dumps(info, sort_keys=True) if args.json else f"Upstream: {info['commits_behind']} commit(s); {info['rebuild_level']}; {info['upstream_head']}; engine tree {info['engine_tree']}")
        if args.command == 'sync':
            if not args.apply:
                print('Preview only. Use --apply to rebase the clean current branch. Dependencies, builds and pushes are separate commands.')
            elif not info['up_to_date']:
                result = subprocess.run(['git', '-C', str(ROOT), 'rebase', info['upstream_head']])
                return result.returncode
    except (RuntimeError, configparser.Error) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
