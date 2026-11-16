"""Real ABI/process tests for the published Wsm.InGame.Runtime Native AOT DLL.

Each process hosts exactly one runtime instance, so host and guest are separate processes.
Everything below uses the actual published DLL, the actual C ABI and real loopback TCP.
No protocol responses are faked.

Usage: python run_dll_tests.py [--dll PATH] [--probe PATH]
"""

import argparse
import ctypes
import os
import socket
import subprocess
import sys
import time

STATE_DISCONNECTED, STATE_HOSTING, STATE_CONNECTING, STATE_CONNECTED, STATE_FAILED = range(5)
ACCEPTED, INVALID_ARGUMENT, UNAVAILABLE, QUEUE_FULL, INSUFFICIENT_CAPACITY = 0, -1, -2, -3, -5
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
# Use the canonical build outputs, not a pre-existing private developer cache.
DEFAULT_DLL = os.path.join(ROOT, "artifacts", "ingame-runtime", "Wsm.InGame.Runtime.dll")
DEFAULT_PROBE = os.path.join(ROOT, "artifacts", "native-build", "Release", "ingame_aot_abi.exe")
REFUSED = 10061


class Runtime:
    """ctypes view of the frozen C ABI."""

    def __init__(self, path):
        self.dll = ctypes.CDLL(path)
        self.dll.wsm_ingame_version.restype = ctypes.c_int
        self.dll.wsm_ingame_host.argtypes = [ctypes.c_int, ctypes.c_int]
        self.dll.wsm_ingame_host.restype = ctypes.c_int
        self.dll.wsm_ingame_join.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_int]
        self.dll.wsm_ingame_join.restype = ctypes.c_int
        for name in ("wsm_ingame_leave", "wsm_ingame_state", "wsm_ingame_peer_count",
                     "wsm_ingame_bound_port", "wsm_ingame_shutdown"):
            getattr(self.dll, name).restype = ctypes.c_int
        for name in ("wsm_ingame_status", "wsm_ingame_last_error"):
            fn = getattr(self.dll, name)
            fn.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
            fn.restype = ctypes.c_int

    def status_bytes(self, size=None):
        if size is None:
            size = self.dll.wsm_ingame_status(None, 0) + 1
        buffer = (ctypes.c_uint8 * max(size, 1))()
        written = self.dll.wsm_ingame_status(buffer, size)
        if written < 0:
            return written, b""
        return written, bytes(buffer[:written])

    def status(self):
        return self.status_bytes()[1].decode("utf-8")

    def error(self):
        required = self.dll.wsm_ingame_last_error(None, 0)
        buffer = (ctypes.c_uint8 * max(required + 1, 1))()
        written = self.dll.wsm_ingame_last_error(buffer, required + 1)
        return bytes(buffer[:max(written, 0)]).decode("ascii")

    def join(self, address, port):
        raw = str(address).encode("ascii")
        buffer = (ctypes.c_uint8 * len(raw)).from_buffer_copy(raw)
        return self.dll.wsm_ingame_join(buffer, len(raw), port)

    def state(self):
        return self.dll.wsm_ingame_state()

    def peers(self):
        return self.dll.wsm_ingame_peer_count()

    def bound(self):
        return self.dll.wsm_ingame_bound_port()

    def host(self, port, lan=0):
        return self.dll.wsm_ingame_host(port, lan)

    def leave(self):
        return self.dll.wsm_ingame_leave()

    def shutdown(self):
        return self.dll.wsm_ingame_shutdown()


failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print("  FAIL " + message)


def wait_for(predicate, message, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.05)
    check(False, message)
    return False


def run_probe(probe, dll, args, timeout=40):
    result = subprocess.run([probe, dll] + [str(a) for a in args],
                            capture_output=True, timeout=timeout)
    text = result.stdout.decode("utf-8", "replace")
    values = {}
    for line in text.replace("\r", "").splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            values[key] = value
    return result.returncode, values, text


def probe_async(probe, dll, args):
    return subprocess.Popen([probe, dll] + [str(a) for a in args],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def collect(process, timeout=40):
    out, _ = process.communicate(timeout=timeout)
    text = out.decode("utf-8", "replace")
    values = {}
    for line in text.replace("\r", "").splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            values[key] = value
    return process.returncode, values, text


def port_open(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1.0):
            return True
    except OSError:
        return False


def port_refused(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1.0):
            return False
    except ConnectionRefusedError:
        return True
    except OSError:
        return False


def listening(port):
    """True while a listening socket for the port exists (netstat is locale dependent)."""
    out = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, text=True,
                         encoding="utf-8", errors="replace").stdout
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 4 and fields[1].endswith(":%d" % port):
            if fields[3].upper() in ("LISTENING", "ABH\u00d6REN"):
                return True
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", default=DEFAULT_DLL)
    parser.add_argument("--probe", default=DEFAULT_PROBE)
    parser.add_argument("--skip-probe", action="store_true")
    options = parser.parse_args()
    dll, probe = options.dll, options.probe
    if not os.path.isfile(dll):
        raise SystemExit("missing DLL: " + dll)
    print("DLL=" + dll)
    api = Runtime(dll)

    print("[1] version and buffer contract")
    check(api.dll.wsm_ingame_version() == 1, "version must be 1")
    required = api.dll.wsm_ingame_status(None, 0)
    check(required > 0, "status query must report a required length")
    check(api.dll.wsm_ingame_status(None, 5) == INVALID_ARGUMENT, "null buffer with capacity > 0")
    check(api.dll.wsm_ingame_status(None, -1) == INVALID_ARGUMENT, "negative capacity")
    exact = (ctypes.c_uint8 * (required + 1))()
    check(api.dll.wsm_ingame_status(exact, required + 1) == required, "write returns required length")
    check(exact[required] == 0, "status must be NUL terminated")
    check(bytes(exact[:required]).decode("utf-8") == "Nicht verbunden.", "idle status text")
    small = (ctypes.c_uint8 * required)()
    check(api.dll.wsm_ingame_status(small, required) == INSUFFICIENT_CAPACITY, "insufficient capacity")
    check(api.dll.wsm_ingame_status(small, 1) == INSUFFICIENT_CAPACITY, "single byte is insufficient")
    one = (ctypes.c_uint8 * 1)()
    check(api.dll.wsm_ingame_status(one, 1) == INSUFFICIENT_CAPACITY, "one byte cannot hold text plus NUL")
    check(api.state() == STATE_DISCONNECTED, "state starts disconnected")
    check(api.error() == "", "no failure code while idle")
    check(api.join("127.0.0.1", 0) == INVALID_ARGUMENT, "join port 0")
    check(api.join("127.0.0.1", 70000) == INVALID_ARGUMENT, "join port 70000")
    check(api.join("999.0.0.1", 7780) == INVALID_ARGUMENT, "invalid IPv4")
    check(api.host(70000, 0) == INVALID_ARGUMENT, "host port 70000")
    check(api.host(-5, 0) == INVALID_ARGUMENT, "host port negative")
    check(api.host(7780, 7) == INVALID_ARGUMENT, "lanBind out of range")

    if not options.skip_probe:
        print("[2] C++ consumer: version via GetProcAddress")
        code, values, text = run_probe(probe, dll, ["version"])
        check(code == 0 and values.get("VERSION") == "1", "C++ probe version: " + text.strip())
        print("[3] C++ consumer: full ABI contract on the published DLL")
        code, values, text = run_probe(probe, dll, ["abi"])
        check(code == 0 and values.get("ABI") == "PASS", "C++ probe abi: " + text.strip())

    print("[4] host on ephemeral port, separate-process guest joins over real TCP")
    check(api.host(0, 0) == ACCEPTED, "host command accepted")
    check(wait_for(lambda: api.state() == STATE_HOSTING, "host reaches hosting"), "hosting")
    port = api.bound()
    check(port > 0, "bound port reported for ephemeral binding")
    check(port_open(port), "listener is actually accepting TCP")
    check("Host aktiv" in api.status(), "German hosting status: " + api.status())
    if not options.skip_probe:
        guest = probe_async(probe, dll, ["join", "127.0.0.1", port, 2500])
        check(wait_for(lambda: api.peers() == 1, "host observes the guest"), "host peer count 1")
        code, values, text = collect(guest)
        check(code == 0 and values.get("STATE") == str(STATE_CONNECTED), "guest process connected: " + text.strip())
        check("Verbunden mit 127.0.0.1:%d" % port in values.get("STATUS", ""), "guest German status")
        check(wait_for(lambda: api.peers() == 0, "host observes the guest leaving"), "host peer count back to 0")

    print("[5] leave releases the listener")
    check(api.leave() == ACCEPTED, "leave accepted")
    check(wait_for(lambda: api.state() == STATE_DISCONNECTED, "state after leave"), "disconnected after leave")
    check(api.peers() == 0, "peer count after leave")
    check(api.bound() == 0, "bound port cleared after leave")
    check("Nicht verbunden" in api.status(), "German disconnected status: " + api.status())
    check(wait_for(lambda: not listening(port), "port must no longer be listening after leave"), "listener closed")
    if not options.skip_probe:
        code, values, text = run_probe(probe, dll, ["join", "127.0.0.1", port, 0])
        check(values.get("ERROR") == "refused",
              "another process must observe the released port as refused: " + text.strip())

    if not options.skip_probe:
        print("[6] refused join is classified, then retry into a live host succeeds")
        dead = 1
        probe_free = socket.socket()
        probe_free.bind(("127.0.0.1", 0))
        dead = probe_free.getsockname()[1]
        probe_free.close()
        code, values, text = run_probe(probe, dll, ["join", "127.0.0.1", dead, 0])
        check(values.get("STATE") == str(STATE_FAILED), "refused join fails: " + text.strip())
        check(values.get("ERROR") == "refused", "refused category: " + text.strip())
        check("abgelehnt" in values.get("STATUS", ""), "German refusal text")

        check(api.host(0, 0) == ACCEPTED, "retry host accepted")
        check(wait_for(lambda: api.state() == STATE_HOSTING, "retry hosting"), "retry hosting")
        retry_port = api.bound()
        code, values, text = run_probe(probe, dll, ["join", "127.0.0.1", retry_port, 400])
        check(values.get("STATE") == str(STATE_CONNECTED), "retry join connects: " + text.strip())
        check(api.leave() == ACCEPTED, "retry leave accepted")
        check(wait_for(lambda: api.state() == STATE_DISCONNECTED, "retry leave"), "retry disconnected")
        check(wait_for(lambda: not listening(retry_port), "retry port closed"), "retry listener closed")

    if not options.skip_probe:
        print("[7] host process death moves the guest to hostlost")
        dead_host = probe_async(probe, dll, ["host", 0, 6000])
        try:
            # The host process prints its bound port as soon as it is hosting.
            start = time.monotonic()
            host_port = None
            while time.monotonic() - start < 10 and host_port is None:
                line = dead_host.stdout.readline().decode("utf-8", "replace")
                if not line:
                    break
                if line.startswith("BOUND=") and line.strip()[6:] not in ("", "0"):
                    host_port = int(line.strip()[6:])
            check(host_port is not None, "host process reported a bound port")
            if host_port:
                check(api.join("127.0.0.1", host_port) == ACCEPTED, "guest join accepted")
                check(wait_for(lambda: api.state() == STATE_CONNECTED, "guest connected to other process"),
                      "guest connected")
                dead_host.kill()
                dead_host.wait(timeout=10)
                check(wait_for(lambda: api.state() == STATE_DISCONNECTED, "guest notices host death", 15),
                      "guest disconnected after host death")
                check(api.error() == "hostlost", "hostlost category: " + api.error())
                check("Verbindung zum Host verloren" in api.status(), "German hostlost text: " + api.status())
        finally:
            if dead_host.poll() is None:
                dead_host.kill()
                dead_host.wait(timeout=10)

    if not options.skip_probe:
        print("[8] full lobby is classified as hostfull")
        check(api.host(0, 0) == ACCEPTED, "lobby host accepted")
        check(wait_for(lambda: api.state() == STATE_HOSTING, "lobby hosting"), "lobby hosting")
        lobby_port = api.bound()
        guests = [probe_async(probe, dll, ["join", "127.0.0.1", lobby_port, 3000]) for _ in range(3)]
        try:
            check(wait_for(lambda: api.peers() == 3, "three real guests connected", 15), "lobby peer count 3")
            code, values, text = run_probe(probe, dll, ["join", "127.0.0.1", lobby_port, 0])
            check(values.get("STATE") == str(STATE_FAILED), "fourth guest rejected: " + text.strip())
            check(values.get("ERROR") == "hostfull", "hostfull category: " + text.strip())
        finally:
            for guest in guests:
                if guest.poll() is None:
                    guest.kill()
            for guest in guests:
                guest.wait(timeout=10)
            api.leave()
            wait_for(lambda: api.state() == STATE_DISCONNECTED, "lobby leave", 10)

    print("[9] shutdown stops the worker and blocks new commands")
    check(api.shutdown() == ACCEPTED, "shutdown accepted")
    check(api.host(7780, 0) == UNAVAILABLE, "host after shutdown is unavailable")
    check(api.join("127.0.0.1", 7780) == UNAVAILABLE, "join after shutdown is unavailable")
    check(api.leave() == UNAVAILABLE, "leave after shutdown is unavailable")
    check(api.shutdown() == ACCEPTED, "second shutdown is idempotent")
    check(len(api.status()) > 0, "status remains queryable after shutdown")

    if failures:
        print("\n%d failure(s)" % len(failures))
        for failure in failures:
            print("  - " + failure)
        return 1
    print("\nPASS native AOT ingame runtime DLL suite")
    return 0


if __name__ == "__main__":
    sys.exit(main())
