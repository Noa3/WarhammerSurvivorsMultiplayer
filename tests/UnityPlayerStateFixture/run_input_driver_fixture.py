"""Run real IL2CPP controller/driver/pump composition, not vendor gameplay."""
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
    "drive_real_controller_start", "drive_actual_owned_window_focus", "drive_actual_mailbox_submit",
    "drive_event_updates_actual_remote_axes", "drive_primary_resources_and_axes_unchanged",
    "drive_actual_lease_expires_without_input_events", "drive_replay_and_wrong_epoch_refused",
    "drive_producer_stop_removes_controller", "drive_completed_client_unsubscribes",
    "drive_start_before_faulted_owner_drop", "drive_live_before_faulted_owner_drop",
    "drive_nonzero_before_cleanup_fault", "drive_actual_cleanup_failure_retains_owner",
    "drive_stop_blocks_new_packets", "drive_actual_cleanup_retries_after_wrapper_gone",
    "drive_cleanup_preserves_primary", "drive_failed_open_is_retained_then_cleaned",
    "drive_start_before_run_end", "drive_run_end_independent_controller_cleanup",
    "drive_run_end_removes_subscription", "drive_start_before_actor_death",
    "drive_dead_actor_independent_controller_cleanup", "drive_dead_actor_removes_subscription",
    "drive_final_resources_and_primary", "drive_mismatched_window_refused_before_acquisition",
    "drive_mismatched_native_thread_refused_before_acquisition", "drive_mismatched_managed_thread_refused_before_acquisition",
    "drive_stop_before_client_start_completes_without_resources", "drive_zero_start",
    "drive_zero_rejects_nonzero_lease", "drive_zero_no_nonzero_attempts",
    "drive_zero_live_cached_resource_on_worker", "drive_zero_rejects_zero_lease",
    "drive_zero_recovery_deadline_closes_without_input", "drive_zero_closed_cached_resource_on_worker",
    "drive_zero_duration_zero_refused_before_acquisition", "drive_zero_duration_over_limit_refused_before_acquisition",
    "drive_zero_start_before_owner_drop", "drive_zero_failed_cleanup_cached_after_owner_drop", "drive_zero_retry_after_owner_drop"
}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--plugin-sha256", required=True)
    args = parser.parse_args()
    player = args.player.resolve()
    plugin = player / "Fixture_Data/Plugins/x86_64/fixture.dll"
    if digest(plugin) != args.plugin_sha256:
        parser.error("Fixture plugin does not match its bound digest")
    output = args.output or Path(__file__).resolve().parents[2] / ".local/input-driver-runs" / uuid.uuid4().hex
    output.mkdir(parents=True, exist_ok=False)
    before = {p.relative_to(player).as_posix(): digest(p) for p in player.rglob("*") if p.is_file()}
    (output / "inputs.json").write_text(json.dumps(before, indent=2) + "\n")
    log = output / "player.log"
    command = [str(player / "Fixture.exe"), "-screen-fullscreen", "0", "-screen-width", "960", "-screen-height", "540", "-logFile", str(log.resolve())]
    with subprocess.Popen(command) as process:
        (output / "owned-player.json").write_text(json.dumps({"pid": process.pid, "parent": os.getpid(),
            "executable": command[0], "fixtureOnly": True}, indent=2) + "\n")
        try:
            process.wait(timeout=150)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise
        player_exit = process.returncode
    text = log.read_text(encoding="utf-8", errors="replace")
    rows = re.findall(r"^DRIVE_TEST ([a-z0-9_]+)=(PASS|FAIL)$", text, re.M)
    after = {p.relative_to(player).as_posix(): digest(p) for p in player.rglob("*") if p.is_file()}
    accepted = player_exit == 0 and Counter(n for n, _ in rows) == Counter(EXPECTED_CASES) and all(s == "PASS" for _, s in rows) and Counter(text.splitlines())["DRIVE_TEST RESULT failures=0"] == 1 and before == after
    receipt = {"accepted": accepted, "playerExit": player_exit, "command": command, "rows": rows,
               "expected": sorted(EXPECTED_CASES), "playerFilesUnchanged": before == after,
               "pluginSha256": digest(plugin), "logSha256": digest(log), "targetGameExecuted": False,
               "vendorRewiredExecuted": False, "actualControllerSessionExecuted": True}
    (output / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("INPUT_DRIVER_FIXTURE accepted", accepted, "rows", len(rows), "playerExit", player_exit, flush=True)
    print("EVIDENCE_DIR", output.resolve(), flush=True)
    return 0 if accepted else 1


if __name__ == "__main__":
    sys.exit(main())
