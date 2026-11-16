"""Exercise the original IL2CPP input-pump fixture and bind its actual files."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

EXPECTED_CASES = {
    "pump_contract", "pump_wrong_window_thread_refused", "pump_wrong_managed_thread_refused",
    "pump_subscribe", "pump_actual_event_delivery", "pump_callback_survives_gc",
    "pump_independent_recovery_runs", "pump_stop_without_more_input_events",
    "pump_late_callback_tombstone", "pump_start_before_wrapper_drop",
    "pump_owner_survives_producer_destruction", "pump_cleanup_retries_after_wrapper_gone",
    "pump_new_owner_after_cleanup", "pump_old_action_never_routes_new_owner",
    "pump_new_action_still_effective", "pump_partial_client_start_retains_cleanup",
    "pump_add_before_effect_failure_recovers", "pump_add_after_effect_failure_recovers",
    "pump_start_before_remove_failure", "pump_remove_failure_retains_subscription",
    "pump_remove_retry_completes", "pump_start_before_ambiguous_remove",
    "pump_remove_after_effect_is_still_pending", "pump_ambiguous_remove_retry_acknowledged",
    "pump_start_before_foreign_callback", "pump_actual_foreign_callback_refused",
    "pump_foreign_callback_cleanup", "pump_start_before_reentry",
    "pump_reentrant_callback_stops_without_recursion", "pump_reentrant_cleanup",
    "pump_start_before_bad_context", "pump_substituted_context_refused",
    "pump_final_no_subscription", "pump_pin_failure_terminal", "pump_retry_after_pin_failure",
    "pump_gate_failure_terminal", "pump_retry_after_gate_failure", "pump_worker_failure_terminal",
    "pump_retry_after_worker_failure", "pump_slot_failure_terminal", "pump_retry_after_slot_failure"
}

def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--plugin-sha256", required=True)
    parser.add_argument("--expect-contract-red", action="store_true")
    args = parser.parse_args()
    player = args.player.resolve()
    exe = player / "Fixture.exe"
    plugin = player / "Fixture_Data/Plugins/x86_64/fixture.dll"
    if not exe.is_file() or digest(plugin) != args.plugin_sha256:
        raise SystemExit("Missing fixture or plugin digest mismatch")
    before = {p.relative_to(player).as_posix(): digest(p) for p in player.rglob("*") if p.is_file()}
    root = Path(__file__).resolve().parents[2]
    output = args.output or root / ".local/input-pump-runs" / uuid.uuid4().hex
    output.mkdir(parents=True, exist_ok=False)
    (output / "inputs.json").write_text(json.dumps(before, indent=2) + "\n")
    log = output / "player.log"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 4
    command = [str(exe), "-screen-fullscreen", "0", "-screen-width", "960", "-screen-height", "540", "-logFile", str(log.resolve())]
    result = subprocess.run(command, startupinfo=startup, timeout=150)
    text = log.read_text(encoding="utf-8", errors="replace")
    rows = re.findall(r"^PUMP_TEST ([a-z0-9_]+)=(PASS|FAIL)$", text, re.M)
    after = {p.relative_to(player).as_posix(): digest(p) for p in player.rglob("*") if p.is_file()}
    if args.expect_contract_red:
        accepted = result.returncode == 1 and rows == [("pump_contract", "FAIL")] and Counter(text.splitlines())["PUMP_TEST RESULT failures=1"] == 1
    else:
        accepted = result.returncode == 0 and Counter(n for n, _ in rows) == Counter(EXPECTED_CASES) and all(s == "PASS" for _, s in rows) and Counter(text.splitlines())["PUMP_TEST RESULT failures=0"] == 1
    accepted = accepted and before == after
    receipt = {"accepted": accepted, "expectedContractRed": args.expect_contract_red, "playerExit": result.returncode,
               "command": command, "rows": rows, "expected": sorted(EXPECTED_CASES), "playerFilesUnchanged": before == after,
               "pluginSha256": digest(plugin), "logSha256": digest(log), "gameOrVendorRewiredExecuted": False}
    (output / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("INPUT_PUMP_FIXTURE", "EXPECTED_CONTRACT_RED" if args.expect_contract_red else "EXECUTION", "accepted", accepted, "rows", len(rows), "playerExit", result.returncode, flush=True)
    print("EVIDENCE_DIR", output.resolve(), flush=True)
    return 0 if accepted else 1

if __name__ == "__main__":
    sys.exit(main())
