"""Run the original player-state IL2CPP fixture without foreground activation."""
import os, re, subprocess, sys
from collections import Counter
EXPECTED_CASES = {"no_manager", "null_stage", "empty_players", "two_players", "populated_stopped",
                  "wrong_native_thread", "wrong_managed_thread", "null_context", "substituted_context",
                  "worker_thread", "over_cap", "non_finite", "late_non_finite", "null_input", "recovered",
                  "stage_root_failure", "players_root_failure",
                  "upgrade_missing_page", "upgrade_second_actor", "upgrade_null_session", "upgrade_null_active",
                  "upgrade_foreign_active", "upgrade_null_collection", "upgrade_null_factory", "upgrade_duplicate_pages",
                  "upgrade_wrong_thread", "upgrade_stopped", "upgrade_late_error", "upgrade_stage_root_failure",
                  "upgrade_recovered", "upgrade_no_manager", "upgrade_duplicate_actor", "upgrade_wrong_collection_type",
                  "upgrade_choice_group_entries", "upgrade_choice_null_list", "upgrade_choice_empty_list",
                  "upgrade_choice_at_bound", "upgrade_choice_over_bound", "upgrade_selection_active",
                  "upgrade_selection_inactive", "upgrade_selection_destroyed", "upgrade_selection_first_active",
                  "upgrade_selection_null_group", "upgrade_selection_wrong_thread", "upgrade_selection_late_error",
                  "upgrade_selection_over_bound", "upgrade_selection_stopped", "upgrade_items_ordered_fields",
                  "upgrade_items_null_group", "upgrade_items_empty_children", "upgrade_items_at_bound",
                  "upgrade_items_over_bound", "upgrade_items_wrong_thread", "upgrade_items_late_error",
                  "upgrade_items_stopped", "upgrade_items_first_container", "upgrade_items_direct_only",
                  "upgrade_items_removed_component", "upgrade_hint_fourth_child", "upgrade_hint_null",
                  "upgrade_hint_foreign", "upgrade_hint_destroyed", "upgrade_hint_no_selection",
                  "upgrade_hint_wrong_thread", "upgrade_hint_late_error", "upgrade_hint_stopped",
                  "upgrade_hint_over_bound", "upgrade_hint_empty_selected"}
root=os.path.abspath(os.path.join(os.path.dirname(__file__),"..",".."))
local=os.path.join(root,".local")
player=os.environ.get("WSM_PLAYER_STATE_PLAYER","player-state-v1-player4")
run=sys.argv[1] if len(sys.argv)>1 else "default"
exe=os.path.join(local,player,"Fixture.exe");log=os.path.join(local,f"player-state-v1-{run}.log")
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=4
args=[exe,"-batchmode","-nographics","-logFile",log]
r=subprocess.run(args,startupinfo=startup,timeout=180)
text=open(log,encoding="utf-8",errors="replace").read() if os.path.exists(log) else ""
passed=text.count("=PASS");failed=text.count("=FAIL");summary="PLAYER_TEST RESULT failures=0" in text
occurrences=Counter(name for name, _ in re.findall(r"PLAYER_TEST ([a-z_]+)=(PASS|FAIL)",text))
ok=r.returncode==0 and summary and failed==0 and passed==len(EXPECTED_CASES) and occurrences==Counter(EXPECTED_CASES)
print("PLAYER_STATE run=%s exit=%s pass=%d fail=%d summary=%s log=%s"%(run,r.returncode,passed,failed,summary,log),flush=True)
sys.exit(0 if ok else 1)
