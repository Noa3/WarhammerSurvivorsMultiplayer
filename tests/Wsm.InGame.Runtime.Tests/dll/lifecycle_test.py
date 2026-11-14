"""Exercise queued commands and leave through the actual Native AOT ABI."""
import argparse
import socket
import subprocess
import time
from run_dll_tests import Runtime, DEFAULT_DLL, DEFAULT_PROBE

p = argparse.ArgumentParser()
p.add_argument('--dll', default=DEFAULT_DLL)
a = p.parse_args()
r = Runtime(a.dll)
listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen()
listener.settimeout(5)
try:
    assert r.join('127.0.0.1', listener.getsockname()[1]) == 0
    peer, _ = listener.accept()
    with peer:
        assert r.host(0) == 0
        assert r.leave() == 0
        time.sleep(1)
        assert r.state() == 0 and r.bound() == 0, f'stale queued host resurrected: state={r.state()} port={r.bound()}'
        assert r.host(0) == 0
        deadline = time.monotonic() + 5
        while r.state() != 1 and time.monotonic() < deadline:
            time.sleep(.02)
        assert r.state() == 1 and r.bound() > 0, f'new host after leave failed: state={r.state()}'
        assert r.leave() == 0
        host = subprocess.Popen([DEFAULT_PROBE, a.dll, 'host', '0', '10000'], stdout=subprocess.PIPE)
        try:
            host_port = None
            while host_port is None:
                line = host.stdout.readline().decode('utf-8', 'replace')
                assert line, 'host probe exited before reporting port'
                if line.startswith('BOUND=') and int(line[6:]) > 0:
                    host_port = int(line[6:])
            assert r.join('127.0.0.1', host_port) == 0
            deadline = time.monotonic() + 5
            while r.state() != 3 and time.monotonic() < deadline:
                time.sleep(.02)
            assert r.state() == 3, f'new join after leave failed: state={r.state()} error={r.error()}'
            assert r.leave() == 0
            print('PASS stale queued host suppressed; new host and join after leave succeed')
        finally:
            host.kill()
            host.wait(timeout=5)
finally:
    listener.close()
    r.shutdown()
