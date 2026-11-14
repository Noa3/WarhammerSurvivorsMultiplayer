"""Runs the owned Unity IL2CPP fixture: the player hosts a real session with the Native AOT DLL,
an external probe process joins it over loopback TCP, and the run is judged from the player log.

Usage: python run_fixture.py [--exe PATH] [--probe PATH] [--dll PATH]
"""

import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_EXE = os.path.join(ROOT, ".local", "ingame-runtime-v1-unity", "Fixture.exe")
DEFAULT_PROBE = os.path.join(ROOT, ".local", "ingame-runtime-v1-build", "probe", "Release", "wsm_ingame_probe.exe")
DEFAULT_DLL = os.path.join(ROOT, ".local", "ingame-runtime-v1-native", "Wsm.InGame.Runtime.dll")
LOG = os.path.join(ROOT, ".local", "ingame-runtime-v1-unity-run.log")


def read_log():
    try:
        with open(LOG, "r", encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except FileNotFoundError:
        return ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=DEFAULT_EXE)
    parser.add_argument("--probe", default=DEFAULT_PROBE)
    parser.add_argument("--dll", default=DEFAULT_DLL)
    options = parser.parse_args()
    for path in (options.exe, options.probe, options.dll):
        if not os.path.isfile(path):
            raise SystemExit("missing file: " + path)
    if os.path.exists(LOG):
        os.remove(LOG)

    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 4  # SW_SHOWNOACTIVATE: never steals focus from the operator
    player = subprocess.Popen([options.exe, "-batchmode", "-nographics", "-logFile", LOG],
                              startupinfo=startup)
    try:
        port = None
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            text = read_log()
            match = re.search(r"GAME_RUNTIME HOSTED_PORT=(\d+)", text)
            if match:
                port = int(match.group(1))
                break
            if player.poll() is not None:
                break
            time.sleep(0.25)
        if port is None:
            print("FIXTURE=FAIL no hosted port")
            print(read_log()[-4000:])
            return 1
        print("UNITY_HOSTED_PORT=%d" % port)

        result = subprocess.run([options.probe, options.dll, "join", "127.0.0.1", str(port), "1500"],
                                capture_output=True, timeout=60)
        guest = result.stdout.decode("utf-8", "replace")
        print("GUEST_STATE_LINE=" + " ".join(line.strip() for line in guest.splitlines()[:1]))
        if "STATE=3" not in guest:
            print("FIXTURE=FAIL external guest could not join the Unity-hosted session")
            print(guest)
            return 1
        code = player.wait(timeout=120)
    finally:
        if player.poll() is None:
            player.kill()
            player.wait(timeout=30)

    text = read_log()
    failures = []
    if code != 0:
        failures.append("player exit code %d" % code)
    if "GAME_RUNTIME VERSION=1" not in text:
        failures.append("version export not 1")
    if "GAME_RUNTIME RESULT=PASS" not in text:
        failures.append("fixture result not PASS")
    if "GAME_RUNTIME PEERS=1" not in text:
        failures.append("player never observed the guest")
    if "Verbunden" not in text:
        failures.append("no German connected status in the player log")
    if failures:
        print("FIXTURE=FAIL " + "; ".join(failures))
        print(text[-4000:])
        return 1
    print("FIXTURE=PASS IL2CPP player hosted the Native AOT DLL and an external guest joined")
    return 0


if __name__ == "__main__":
    sys.exit(main())
