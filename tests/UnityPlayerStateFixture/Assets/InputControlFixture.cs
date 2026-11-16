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
        UnityEngine.Object.DestroyImmediate(input.gameObject);
    }
}
