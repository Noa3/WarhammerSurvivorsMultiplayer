"""Verify exact named cases from the standalone IL2CPP roster fixture."""
import collections
import os
import re
import subprocess
import sys

EXPECTED = {"inspect", "unknown", "already", "enroll", "repeat", "rollback",
            "wrong_os", "wrong_managed", "null_context", "add_before", "add_after",
            "remove_retry", "manager_replaced", "select_unknown", "select_target", "select_original",
            "copy_unknown", "copy_original", "copy_target", "copy_unbought"}
root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
exe = os.environ.get("WSM_COOP_FIXTURE_PLAYER", os.path.join(root, ".local", "coop-roster-v2-player", "Fixture.exe"))
log = os.environ.get("WSM_COOP_FIXTURE_LOG", os.path.join(root, ".local", "coop-roster-v2-run.log"))
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 4
result = subprocess.run([exe, "-batchmode", "-nographics", "-logFile", log], startupinfo=startup, timeout=180)
with open(log, encoding="utf-8", errors="replace") as stream:
    text = stream.read()
entries = re.findall(r"COOP_ROSTER_TEST ([a-z_]+)=(PASS|FAIL)", text)
counts = collections.Counter(name for name, _ in entries)
ok = (result.returncode == 0 and counts == collections.Counter(EXPECTED)
      and all(outcome == "PASS" for _, outcome in entries)
      and "COOP_ROSTER_TEST RESULT failures=0" in text)
print("COOP_ROSTER exit=%s cases=%s pass=%s summary=%s log=%s" %
      (result.returncode, len(entries), sum(outcome == "PASS" for _, outcome in entries), ok, log))
sys.exit(0 if ok else 1)
