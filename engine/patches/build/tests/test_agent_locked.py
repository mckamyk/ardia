#!/usr/bin/env python3
import os
import pathlib
import platform
import shutil
import signal
import subprocess
import tempfile
import time
import unittest

REPO = pathlib.Path(__file__).resolve().parents[4]
LOCKED = REPO / 'scripts/agent/locked'


@unittest.skipUnless(platform.system() == 'Linux' and shutil.which('flock'), 'Linux flock path')
class LinuxLockedTests(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='ac-locked-'))
        self.env = dict(os.environ, AC_LOCK_ROOT=str(self.root))
        for key in ('AC_LOCKED_HELD', 'AC_LOCKED_AS', 'AC_LOCKED_INNER', 'AC_LOCKED_DEADLINE'):
            self.env.pop(key, None)
        self.children = []

    def tearDown(self):
        for child in self.children:
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=10)
        shutil.rmtree(self.root, ignore_errors=True)

    def locked(self, *args, timeout=30):
        return subprocess.run([str(LOCKED), *args], env=self.env, text=True, capture_output=True, timeout=timeout)

    def background(self, *args):
        child = subprocess.Popen([str(LOCKED), *args], env=self.env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        self.children.append(child)
        return child

    def wait_for(self, predicate, seconds=10):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if predicate():
                return True
            time.sleep(0.05)
        return False

    def test_runs_command_with_kernel_lock_holder_and_legacy_dir(self):
        result = self.locked('pod', '--wait', '5', '--', 'sh', '-c', 'cat "$AC_LOCK_ROOT/ac-pod.holder"; test -d "$AC_LOCK_ROOT/ac-pod.lock" && echo legacy')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r'^pid \d+ since \d\d:\d\d:\d\d in .*: sh -c')
        self.assertIn('legacy', result.stdout)
        self.assertTrue((self.root / 'ac-pod.lockf').exists())
        self.assertFalse((self.root / 'ac-pod.lock').exists())
        self.assertFalse((self.root / 'ac-pod.holder').exists())

    def test_exit_status_passes_through(self):
        self.assertEqual(self.locked('chromium', '--', 'sh', '-c', 'exit 3').returncode, 3)

    def test_busy_lock_times_out_with_75_and_names_holder(self):
        holder = self.background('chromium', '--', 'sleep', '30')
        self.assertTrue(self.wait_for(lambda: (self.root / 'ac-chromium.holder').exists()))
        started = time.monotonic()
        result = self.locked('chromium', '--wait', '2', '--', 'true')
        self.assertEqual(result.returncode, 75, result.stderr)
        self.assertLess(time.monotonic() - started, 15)
        self.assertIn('still busy', result.stderr)
        self.assertIn('held by pid', result.stderr)
        self.assertIsNone(holder.poll())

    def test_lock_dies_with_its_holder(self):
        holder = self.background('chromium', '--', 'sleep', '30')
        self.assertTrue(self.wait_for(lambda: (self.root / 'ac-chromium.holder').exists()))
        os.killpg(holder.pid, signal.SIGKILL)
        holder.wait(timeout=10)
        result = self.locked('chromium', '--wait', '10', '--', 'echo', 'after')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'after')
        self.assertIn('stale', result.stderr)

    def test_nested_call_runs_inside_enclosing_hold(self):
        inner = f'"{LOCKED}" chromium --wait 3 -- echo nested'
        result = self.locked('chromium', '--wait', '5', '--', 'sh', '-c', inner)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'nested')
        self.assertIn('already held by an enclosing call', result.stderr)

    def test_take_renew_describe_and_release(self):
        taken = self.locked('perflab', '--take', 'lane', '--ttl', '5')
        self.assertEqual(taken.returncode, 0, taken.stderr)
        first = (self.root / 'ac-perflab.holder').read_text().split()
        self.assertEqual(first[0], 'take')
        self.assertEqual(first[2], 'lane')
        busy = self.locked('perflab', '--wait', '1', '--', 'true')
        self.assertEqual(busy.returncode, 75)
        self.assertRegex(busy.stderr, r'taken by lane since .*, until \d\d:\d\d:\d\d')
        renewed = self.locked('perflab', '--take', 'lane', '--ttl', '30')
        self.assertEqual(renewed.returncode, 0, renewed.stderr)
        self.assertGreater(int((self.root / 'ac-perflab.holder').read_text().split()[1]), int(first[1]))
        own = self.locked('perflab', '--as', 'lane', '--', 'echo', 'mine')
        self.assertEqual(own.returncode, 0, own.stderr)
        self.assertEqual(own.stdout.strip(), 'mine')
        released = self.locked('perflab', '--release', 'lane')
        self.assertEqual(released.returncode, 0, released.stderr)
        self.assertFalse((self.root / 'ac-perflab.holder').exists())
        self.assertEqual(self.locked('perflab', '--wait', '2', '--', 'true').returncode, 0)

    def test_expired_take_counts_as_abandoned(self):
        (self.root / 'ac-pod.lock').mkdir()
        (self.root / 'ac-pod.holder').write_text(f'take {int(time.time()) - 5} old since 00:00:00\n')
        result = self.locked('pod', '--wait', '5', '--', 'echo', 'ok')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("old's take expired", result.stderr)

    def test_legacy_mkdir_holder_is_described(self):
        (self.root / 'ac-pod.lock').mkdir()
        result = self.locked('pod', '--wait', '2', '--', 'true')
        self.assertEqual(result.returncode, 75)
        self.assertIn('old mkdir lock', result.stderr)

    def test_xcodebuild_derived_data_lock_name(self):
        dd = self.root / 'dd'
        dd.mkdir()
        result = self.locked('xcodebuild', '--wait', '5', '--', 'sh', '-c', 'ls "$AC_LOCK_ROOT"', '-derivedDataPath', str(dd))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r'ac-xcodebuild-dd-[0-9a-f]{8}\.lockf')

    def test_pod_holds_the_pods_lock_exclusively(self):
        result = self.locked('pod', '--wait', '5', '--', 'sh', '-c', 'flock -n -s "$AC_LOCK_ROOT/ac-pods.rw" true; echo $?')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '1')


if __name__ == '__main__':
    unittest.main()
