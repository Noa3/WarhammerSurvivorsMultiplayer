"""Run the original player-state IL2CPP fixture without foreground activation."""
import os, re, subprocess, sys
from collections import Counter
EXPECTED_CASES = {"no_manager", "null_stage", "empty_players", "two_players", "populated_stopped",
                  "run_gate_no_manager", "run_gate_running_without_stage", "run_gate_wrong_thread", "run_gate_positively_stopped",
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
                  "upgrade_hint_over_bound", "upgrade_hint_empty_selected", "equipment_cross_image_contract", "equipment_two_owners",
                  "equipment_null_manager", "equipment_null_collection", "equipment_empty_collection",
                  "equipment_null_owner", "equipment_foreign_owner", "equipment_non_weapon", "equipment_at_bound",
                  "equipment_over_bound", "equipment_late_owner_error", "equipment_changing_count", "equipment_count_callback",
                  "equipment_count_reject_other_error", "equipment_wrong_thread",
                  "equipment_stopped", "equipment_duplicate_actor", "equipment_duplicate_input",
                  "equipment_over_player_cap", "equipment_null_stage", "equipment_no_manager",
                  "input_profile_no_manager", "input_profile_two_definitions", "input_profile_not_ready",
                  "input_profile_wrong_native", "input_profile_wrong_managed", "input_profile_empty_definitions",
                  "input_profile_null_ids", "input_profile_duplicate_source", "input_profile_negative_source",
                  "input_profile_missing_definition", "input_profile_late_axis_error", "input_profile_null_axes",
                  "input_profile_duplicate_axis", "input_profile_negative_axis", "input_profile_one_axis",
                  "input_profile_axes_at_bound", "input_profile_axes_over_bound", "input_profile_definitions_at_bound",
                  "input_profile_definitions_over_bound", "input_profile_missing_action", "input_profile_duplicate_managers",
                  "control_controller_id_readonly_field_contract", "control_environment", "control_refuse_primary", "control_refuse_source", "control_refuse_category",
                  "control_refuse_layout", "control_refuse_duplicate_axes", "control_open_neutral", "control_refuse_duplicate_open",
                  "control_refuse_nan", "control_mapped_remote_axes", "control_neutral", "control_cleanup",
                  "control_owned_resources_absent", "control_primary_resources_preserved", "control_wrong_os_thread_atomic",
                  "control_missing_map_rollback", "control_binding_failure_rollback", "control_populated_map_rollback",
                  "control_enable_failure_rollback", "control_registered_map_failure_rollback", "control_partial_failures_absent", "control_reopen_after_failures",
                  "control_reject_replaced_input_identity", "control_reject_stopped_run", "control_reject_failed_axis_write",
                  "control_resume_after_refused_writes", "control_cleanup_zero_setter_failure", "control_cleanup_fallback_neutral",
                  "control_open_before_replaced_player_cleanup", "control_close_replaced_player", "control_replaced_player_resources_removed",
                  "control_open_before_foreign_map", "control_refuse_foreign_map_collision", "control_foreign_map_collision_preserved",
                  "control_close_after_foreign_map_removed"}
EXPECTED_CASES.update({"choice_secondary_owner_once", "choice_replay_never_calls_again",
    "choice_wrong_owner_capture_atomic", "choice_wrong_owner_submit", "choice_wrong_weapon_submit",
    "choice_decorative_child_refused", "choice_out_of_range_refused", "choice_zero_token_refused",
    "choice_recapture_invalidates_old_token", "choice_changed_data_refused", "choice_changed_card_level_refused",
    "choice_changed_active_owner_refused", "choice_replaced_actor_same_id_refused", "choice_replaced_stage_refused",
    "choice_stopped_run_refused", "choice_dead_actor_refused", "choice_disabled_card_refused",
    "choice_animating_page_refused", "choice_destroyed_card_refused", "choice_wrong_page_association_refused",
    "choice_wrong_os_thread_refused", "choice_callback_exception_consumes_offer", "choice_closed_offer_refused",
    "choice_close_after_stopped_run", "choice_limitless_card_refused",
    "choice_zero_weapon_library_refused", "choice_negative_weapon_library_refused"})
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
