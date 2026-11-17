using System;
using System.Runtime.InteropServices;
using UnityEngine;
using VampireSurvivors.Framework;

public static class InputControlFixture
{
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_control(int managed, uint os, int mode);
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern IntPtr fixture_control_reason();
    public static void Run(Action<string, int> report, int managed, uint os, GameManager manager)
    {
        void Check(string name, int mode)
        {
            var result = fixture_control(managed, os, mode);
            if (result != 0) Debug.Log("CONTROL_REASON " + Marshal.PtrToStringAnsi(fixture_control_reason()));
            report(name, result);
        }
        var input = new GameObject("ControlInputManager").AddComponent<Rewired.FixtureInputManager>();
        var data = new Rewired.Data.UserData { ids = new[] { 7 } };
        data.definitions.Add(7, new Rewired.Data.CustomController_Editor { axisIds = new[] { 2, 9 } });
        input.Configure(data);
        manager.running = true;
        manager.players[0].input = new Rewired.Player(0);
        manager.players[1].input = new Rewired.Player(1);
        manager.players[0].dead = manager.players[1].dead = false;
        manager.players[0].hp = manager.players[1].hp = 100;
        var primary = manager.players[0].input;
        var remote = manager.players[1].input;
        var idField=typeof(Rewired.Controller).GetField("id");
        report("control_controller_id_readonly_field_contract",idField!=null&&idField.DeclaringType==typeof(Rewired.Controller)&&
            idField.FieldType==typeof(int)&&idField.IsPublic&&idField.IsInitOnly&&typeof(Rewired.Controller).GetProperty("id")==null?0:103);
        Rewired.ReInput.playerHelper.values.Add(0, primary);
        Rewired.ReInput.playerHelper.values.Add(1, remote);
        var keyboard = new Rewired.ControllerWithAxes(10, Rewired.ControllerType.Keyboard, new[] { 2, 9 });
        primary.controllers.AddController(keyboard, false);
        var originalMap = new Rewired.ControllerMap(keyboard, 17, 23);
        originalMap.CreateElementMap(Rewired.ElementAssignment.FullAxisAssignment(2, 101, false));
        originalMap.CreateElementMap(Rewired.ElementAssignment.FullAxisAssignment(9, 103, false));
        primary.controllers.maps.AddMap(keyboard, originalMap, true);
        Check("control_environment", 0);
        Check("control_source_observed_empty",30);
        var sourceA=new Rewired.CustomController(901,7,new[] {2,9});
        var sourceB=new Rewired.CustomController(902,7,new[] {9,2});
        primary.controllers.AddController(sourceA,false); primary.controllers.AddController(sourceB,false);
        var sourceMap=new Rewired.ControllerMap(sourceA,17,23);
        primary.controllers.maps.AddMap(sourceA,sourceMap,false);
        var reads=Rewired.CustomController.SourceReads;
        Check("control_source_inventory",21);
        report("control_source_getter_called",Rewired.CustomController.SourceReads==reads+2?0:126);
        Rewired.CustomController.FailSource=true;
        try {
            reads=Rewired.CustomController.SourceReads;
            Check("control_default_inspect_ignores_source_throw",23);
            report("control_default_inspect_no_source_reads",Rewired.CustomController.SourceReads==reads?0:127);
            Check("control_source_throw_atomic",22);
        } finally { Rewired.CustomController.FailSource=false; }
        sourceB.FixtureIdentifiers(9,9);
        try { Check("control_source_duplicate_axis_atomic",24); } finally { sourceB.FixtureIdentifiers(9,2); }
        sourceB.FixtureIdentifiers(-1,2);
        try { Check("control_source_negative_axis_atomic",25); } finally { sourceB.FixtureIdentifiers(9,2); }
        sourceMap.fixtureOwnerOverride=keyboard;
        try { Check("control_source_map_owner_atomic",26); } finally { sourceMap.fixtureOwnerOverride=null; }
        var badSource=new Rewired.CustomController(903,-1,new[] {2,9}); primary.controllers.AddController(badSource,false);
        try { Check("control_source_negative_id_atomic",27); } finally { primary.controllers.RemoveController(badSource); }
        var duplicate=new Rewired.CustomController(902,7,new[] {2,9}); primary.controllers.AddController(duplicate,false);
        try { Check("control_source_duplicate_controller_atomic",28); } finally { primary.controllers.RemoveController(duplicate); }
        var duplicateMap=new Rewired.ControllerMap(sourceA,17,23) { fixtureIdOverride=sourceMap.id };
        primary.controllers.maps.AddMap(sourceA,duplicateMap,false);
        try { Check("control_source_duplicate_map_atomic",29); } finally { primary.controllers.maps.GetMaps(sourceA).Remove(duplicateMap); }
        var badKind=new Rewired.Controller(904,Rewired.ControllerType.Custom); primary.controllers.AddController(badKind,false);
        try { Check("control_source_kind_mismatch_atomic",31); } finally { primary.controllers.RemoveController(badKind); }
        report("control_source_no_mutation",primary.controllers.ContainsController(sourceA)&&primary.controllers.ContainsController(sourceB)&&
            primary.controllers.maps.GetMaps(sourceA).Count==1&&ReferenceEquals(primary.controllers.maps.GetMaps(sourceA)[0],sourceMap)&&
            !sourceMap.enabled&&primary.controllers.maps.GetMaps(sourceB).Count==0&&Rewired.ReInput.controllerHelper.values.Count==0?0:128);
        primary.controllers.maps.GetMaps(sourceA).Clear(); primary.controllers.RemoveController(sourceA); primary.controllers.RemoveController(sourceB);
        Check("control_refuse_primary", 5);
        Check("control_refuse_source", 6);
        Check("control_refuse_category", 7);
        Check("control_refuse_layout", 8);
        Check("control_refuse_duplicate_axes", 9);
        Check("control_open_neutral", 1);
        Check("control_refuse_duplicate_open", 10);
        Check("control_refuse_nan", 11);
        Check("control_mapped_remote_axes", 2);
        Check("control_neutral", 3);
        Check("control_cleanup", 4);
        report("control_owned_resources_absent", Rewired.ReInput.controllerHelper.values.Count == 0 &&
            !System.Linq.Enumerable.Any(remote.controllers.Controllers) ? 0 : 87);
        report("control_primary_resources_preserved", primary.controllers.ContainsController(keyboard) &&
            primary.controllers.maps.GetMaps(keyboard).Count == 1 &&
            ReferenceEquals(primary.controllers.maps.GetMaps(keyboard)[0], originalMap) && originalMap.enabled ? 0 : 88);
        Check("control_wrong_os_thread_atomic",15);
        Rewired.ControllerMap.FailCreate=true;
        try { Check("control_missing_map_rollback",12); }
        finally { Rewired.ControllerMap.FailCreate=false; }
        Rewired.ControllerMap.FailElementCreate=true;
        try { Check("control_binding_failure_rollback",12); }
        finally { Rewired.ControllerMap.FailElementCreate=false; }
        Rewired.ControllerMap.Prepopulate=true;
        try { Check("control_populated_map_rollback",12); }
        finally { Rewired.ControllerMap.Prepopulate=false; }
        Rewired.ControllerMap.FailEnable=true;
        try { Check("control_enable_failure_rollback",12); }
        finally { Rewired.ControllerMap.FailEnable=false; }
        Rewired.Player.ControllerHelper.MapHelper.FailAfterEmptyAdd=true;
        try { Check("control_registered_map_failure_rollback",12); }
        finally { Rewired.Player.ControllerHelper.MapHelper.FailAfterEmptyAdd=false; }
        report("control_partial_failures_absent",Rewired.ReInput.controllerHelper.values.Count==0 &&
            !System.Linq.Enumerable.Any(remote.controllers.Controllers)?0:96);
        Check("control_reopen_after_failures",1);
        var replacement=new Rewired.Player(1);
        manager.players[1].input=replacement;Rewired.ReInput.playerHelper.values[1]=replacement;
        try { Check("control_reject_replaced_input_identity",13); }
        finally { manager.players[1].input=remote;Rewired.ReInput.playerHelper.values[1]=remote; }
        manager.running=false;
        try { Check("control_reject_stopped_run",14); }
        finally { manager.running=true; }
        Rewired.CustomController.FailWrites=true;
        try { Check("control_reject_failed_axis_write",14); }
        finally { Rewired.CustomController.FailWrites=false; }
        Check("control_resume_after_refused_writes",2);
        Rewired.CustomController.FailZero=true;
        try { Check("control_cleanup_zero_setter_failure",4); }
        finally { Rewired.CustomController.FailZero=false; }
        report("control_cleanup_fallback_neutral",remote.GetAxis("Move Horizontal")==0 && remote.GetAxis("Move Vertical")==0 &&
            Rewired.ReInput.controllerHelper.values.Count==0&&!System.Linq.Enumerable.Any(remote.controllers.Controllers)?0:97);
        Check("control_open_before_replaced_player_cleanup",1);
        Rewired.ReInput.playerHelper.values[1]=replacement;
        try { Check("control_close_replaced_player",4); }
        finally { Rewired.ReInput.playerHelper.values[1]=remote; }
        report("control_replaced_player_resources_removed",!System.Linq.Enumerable.Any(remote.controllers.Controllers)?0:100);
        // Keep the original failing assertion above; clean only this fixture's
        // retained resources so the following collision experiment still runs.
        foreach(var stale in System.Linq.Enumerable.ToArray(remote.controllers.Controllers))
        {
            remote.controllers.maps.GetMaps(stale).Clear();remote.controllers.RemoveController(stale);
        }
        Check("control_open_before_foreign_map",1);
        var owned=System.Linq.Enumerable.FirstOrDefault(remote.controllers.Controllers);
        var ownMap=owned!=null&&remote.controllers.maps.GetMaps(owned).Count!=0?remote.controllers.maps.GetMaps(owned)[0]:null;
        var foreignMap=ownMap!=null?new Rewired.ControllerMap(owned,17,23) {fixtureIdOverride=ownMap.id}:null;
        if(foreignMap!=null)remote.controllers.maps.AddMap(owned,foreignMap,true);
        Check("control_refuse_foreign_map_collision",17);
        report("control_foreign_map_collision_preserved",owned!=null&&ownMap!=null&&remote.controllers.maps.GetMaps(owned).Count==2 &&
            ReferenceEquals(remote.controllers.maps.GetMaps(owned)[0],ownMap)&&ReferenceEquals(remote.controllers.maps.GetMaps(owned)[1],foreignMap)&&
            remote.controllers.ContainsController(owned)&&Rewired.ReInput.controllerHelper.values.Count==1?0:101);
        if(owned!=null&&foreignMap!=null)remote.controllers.maps.GetMaps(owned).Remove(foreignMap);
        Check("control_close_after_foreign_map_removed",4);
        Check("control_open_before_primary_change",1);
        originalMap.enabled=false;
        try { Check("control_close_primary_changed_release_proven",19); }
        finally { originalMap.enabled=true; }
        Check("control_open_before_primary_query_failure",1);
        originalMap.fixtureFailEnabledRead=true;
        try { Check("control_close_primary_query_failure_release_proven",19); }
        finally { originalMap.fixtureFailEnabledRead=false; }
        var foreign = new Rewired.CustomController(9000, 7, new[] { 2, 9 });
        Rewired.ReInput.controllerHelper.values.Add(foreign);
        Rewired.ReInput.ControllerHelper.FailAfterCreate=true;
        try { Check("control_factory_register_then_throw_retains_ownership",18); }
        finally { Rewired.ReInput.ControllerHelper.FailAfterCreate=false; }
        report("control_factory_ambiguous_preserves_global",Rewired.ReInput.controllerHelper.values.Count==2 &&
            ReferenceEquals(Rewired.ReInput.controllerHelper.values[0],foreign)?0:108);
        Rewired.ReInput.ControllerHelper.FailAfterCreate=true;
        Rewired.ReInput.ControllerHelper.FailGetAfterCreate=true;
        try { Check("control_factory_throw_global_query_failure_retains_ownership",20); }
        finally { Rewired.ReInput.ControllerHelper.FailAfterCreate=false; Rewired.ReInput.ControllerHelper.FailGetAfterCreate=false; Rewired.ReInput.ControllerHelper.FailGet=false; }
        report("control_factory_query_failure_preserves_foreign",Rewired.ReInput.controllerHelper.values.Count==3 &&
            ReferenceEquals(Rewired.ReInput.controllerHelper.values[0],foreign)?0:115);
        UnityEngine.Object.DestroyImmediate(input.gameObject);
    }
}
