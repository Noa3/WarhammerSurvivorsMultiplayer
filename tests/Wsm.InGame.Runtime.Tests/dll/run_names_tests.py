"""Exercise display names through the real NativeAOT ABI and separate TCP peers."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from run_dll_tests import Runtime, ROOT


class NamedRuntime(Runtime):
    def __init__(self, path):
        super().__init__(path)
        self.dll.wsm_ingame_set_player_name.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
        self.dll.wsm_ingame_set_player_name.restype = ctypes.c_int
        for name in ('wsm_ingame_local_player_name', 'wsm_ingame_player_names'):
            fn = getattr(self.dll, name)
            fn.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
            fn.restype = ctypes.c_int

    def set_name(self, value):
        raw = value.encode('utf-8') if isinstance(value, str) else value
        buffer = (ctypes.c_uint8 * max(len(raw), 1))()
        if raw:
            ctypes.memmove(buffer, raw, len(raw))
        return self.dll.wsm_ingame_set_player_name(buffer, len(raw))

    def text(self, export):
        fn = getattr(self.dll, export)
        for _ in range(5):
            size = fn(None, 0)
            assert 0 <= size <= 4096, (export, size)
            buffer = (ctypes.c_uint8 * (size + 1))()
            written = fn(buffer, len(buffer))
            if written == -5:
                continue
            assert 0 <= written <= size and buffer[written] == 0, (export, written)
            raw = bytes(buffer[:written])
            assert b'\0' not in raw
            return raw.decode('utf-8', errors='strict')
        raise AssertionError('Name buffer never stabilized')

    def local_name(self):
        return self.text('wsm_ingame_local_player_name')

    def names(self):
        return self.text('wsm_ingame_player_names')


def wait(predicate, message, timeout=12):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.02)
    raise AssertionError(message)


def guest(args):
    api = NamedRuntime(args.dll)
    try:
        assert api.set_name(args.name) == 0
        assert api.join('127.0.0.1', args.guest) == 0
        wait(lambda: api.state() == 3 and api.local_name() == args.expected_name and args.host_name in api.names().splitlines(), 'Guest named admission failed')
        Path(args.ready).write_text(json.dumps({'local': api.local_name(), 'names': api.names()}, ensure_ascii=True))
        if args.expect_loss:
            wait(lambda: api.state() == 0 and api.error() == 'hostlost' and api.names() == '' and api.local_name() == '', 'Host loss retained names', 20)
        else:
            wait(lambda: Path(args.stop).is_file(), 'Guest stop was not requested', 25)
            assert api.leave() == 0
            wait(lambda: api.state() == 0 and api.names() == '' and api.local_name() == '', 'Leave retained names')
        Path(args.done).write_text(json.dumps({'local': api.local_name(), 'names': api.names(), 'state': api.state()}, ensure_ascii=True))
    finally:
        assert api.shutdown() == 0
    print('NAME_GUEST_COMPLETE', flush=True)


def main(args):
    dll = Path(args.dll).resolve()
    before = hashlib.sha256(dll.read_bytes()).hexdigest()
    api = NamedRuntime(str(dll))
    children = []
    scratch = Path(os.environ.get('TMPDIR') or (Path(ROOT) / '.local'))
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='wsm-native-names-', dir=scratch) as directory:
        work = Path(directory)

        def start_guest(index, name, expected, host, expect_loss=False):
            ready, stop, done = (work / (str(index) + suffix) for suffix in ('.ready.json', '.stop', '.done.json'))
            command = [sys.executable, str(Path(__file__).resolve()), '--dll', str(dll), '--guest', str(api.bound()), '--name', name,
                       '--expected-name', expected, '--host-name', host, '--ready', str(ready), '--stop', str(stop), '--done', str(done)]
            if expect_loss:
                command.append('--expect-loss')
            env = os.environ.copy()
            env['PYTHONIOENCODING'] = 'utf-8'
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
            children.append((process, stop))
            wait(lambda: ready.is_file() or process.poll() is not None, 'Guest readiness timeout')
            if not ready.is_file():
                output, _ = process.communicate(timeout=10)
                raise AssertionError(output.decode('utf-8', 'replace'))
            return process, stop, done, json.loads(ready.read_text())

        def finish_guest(process, done):
            output, _ = process.communicate(timeout=25)
            assert process.returncode == 0 and b'NAME_GUEST_COMPLETE' in output, output.decode('utf-8', 'replace')
            record = json.loads(done.read_text())
            assert record['names'] == '' and record['local'] == ''

        try:
            assert api.names() == '' and api.local_name() == ''
            host = 'Host <b>Captain</b>'
            assert api.set_name(host) == 0
            assert api.set_name(b'\xc0\xaf') == -1
            assert api.set_name(b'x' * 4097) == -1
            assert api.host(0) == 0
            wait(lambda: api.state() == 1 and api.local_name() == host and api.names() == host, 'Host name missing')
            print('PASS native_names_host_identity_and_strict_utf8', flush=True)
            for export in ('wsm_ingame_local_player_name', 'wsm_ingame_player_names'):
                fn = getattr(api.dll, export)
                sentinel = (ctypes.c_uint8 * 1)(0xa5)
                assert fn(None, 1) == -1 and fn(None, -1) == -1
                assert fn(sentinel, 1) == -5 and sentinel[0] == 0xa5
            print('PASS native_names_buffer_contract', flush=True)
            assert api.set_name('Next Host') == 0 and api.local_name() == host
            process, stop, done, actual = start_guest(1, 'Gast😀\nRune', 'Gast😀Rune', host)
            wait(lambda: api.peers() == 1 and api.names() == host + '\nGast😀Rune', 'Host roster did not receive guest name')
            assert actual == {'local': 'Gast😀Rune', 'names': host + '\nGast😀Rune'}
            print('PASS native_names_unicode_literal_roster_across_processes', flush=True)
            assert api.local_name() == host
            print('PASS native_names_pending_profile_does_not_rename_live_session', flush=True)
            stop.touch()
            finish_guest(process, done)
            wait(lambda: api.peers() == 0 and api.names() == host, 'Departed guest remained in host roster')
            print('PASS native_names_leave_clears_departed_identity', flush=True)
            assert api.leave() == 0
            wait(lambda: api.state() == 0 and api.names() == '' and api.local_name() == '', 'Host leave did not clear names')
            assert api.host(0) == 0
            wait(lambda: api.state() == 1 and api.local_name() == 'Next Host' and api.names() == 'Next Host', 'Next admission did not use captured profile')
            print('PASS native_names_next_admission_uses_new_profile', flush=True)
            process, stop, done, _ = start_guest(2, 'Watcher', 'Watcher', 'Next Host', expect_loss=True)
            assert api.shutdown() == 0
            finish_guest(process, done)
            print('PASS native_names_host_loss_clears_remote_roster', flush=True)
            assert hashlib.sha256(dll.read_bytes()).hexdigest() == before
            print('PASS NativeAOT display-name ABI and real multi-process TCP; no Steam/gameplay claim', flush=True)
        finally:
            for process, stop in children:
                if process.poll() is None:
                    stop.touch()
                    try:
                        process.communicate(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()  # Only this test's own Popen child.
                        process.communicate(timeout=5)
                        raise AssertionError('Owned name-test child required forced cleanup')
            assert api.shutdown() == 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--dll', default=str(Path(ROOT) / 'artifacts/ingame-runtime/Wsm.InGame.Runtime.dll'))
    parser.add_argument('--guest', type=int)
    parser.add_argument('--name')
    parser.add_argument('--expected-name')
    parser.add_argument('--host-name')
    parser.add_argument('--ready')
    parser.add_argument('--stop')
    parser.add_argument('--done')
    parser.add_argument('--expect-loss', action='store_true')
    options = parser.parse_args()
    guest(options) if options.guest else main(options)
