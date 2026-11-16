using System;
using System.Runtime.InteropServices;
using UnityEngine;
using Unity.Mathematics;
using VampireSurvivors.Data;
using VampireSurvivors.Framework;
using VampireSurvivors.Objects;
using Character = VampireSurvivors.Objects.Characters.CharacterController;

// Exercises the real native controller through an original managed IL2CPP contract.
public static class CameraFixture
{
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_camera_init(int managed,uint os);
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_camera_open(int id,int mode,int expectSuccess);
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_camera_mode(int mode);
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern ulong fixture_camera_updates();
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_camera_active(int expected);
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_camera_state(int has,int spectating,int id,float x,float y,ulong before);
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_camera_wrong_thread();
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_camera_close();
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_camera_close_keep();
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_camera_diagnostic(int expected);
    [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern IntPtr fixture_camera_reason();

    public static GameManager Run(Action<string,int> report,int managed,uint os,GameManager gm)
    {
        void Check(string name,bool condition,int native=0)
        {
            if(!condition || native!=0) Debug.Log("CAMERA_REASON "+Marshal.PtrToStringAnsi(fixture_camera_reason()));
            report(name,condition && native==0?0:native!=0?native:120);
        }
        bool At(Transform t,float x,float y,float z) => t && Mathf.Abs(t.position.x-x)<0.001f && Mathf.Abs(t.position.y-y)<0.001f && Mathf.Abs(t.position.z-z)<0.001f;
        Character Actor(int id,float x,float y) => new Character { input=new Rewired.Player(id),kind=CharacterType.A,level=5,hp=100,xp=1,value=new float2(x,y) };
        var first=Actor(41,2,4);var local=Actor(52,10,12);var third=Actor(77,-8,-10);
        gm.running=true;gm.stage=new Stage();gm.players=new System.Collections.Generic.List<Character>{first,local};
        var target=new GameObject("CameraTarget");var replacement=new GameObject("ReplacementCameraTarget");
        gm.fixtureCameraTarget=target.transform;gm.fixtureNormalCamera=true;target.transform.position=new Vector3(100,200,-17);
        int foreignCalls=0;Action foreign=()=>++foreignCalls;
        int init=fixture_camera_init(managed,os);
        gm.ManualCameraTargetControl=foreign;
        int refused=fixture_camera_open(52,0,0);
        Check("camera_foreign_open_refused",init==0 && refused==0 && ReferenceEquals(gm.ManualCameraTargetControl,foreign),refused);
        gm.ManualCameraTargetControl=null;gm.fixtureNormalCamera=false;
        int notNormal=fixture_camera_open(52,0,0);
        Check("camera_not_normal_refused",notNormal==0 && gm.ManualCameraTargetControl==null,notNormal);
        gm.fixtureNormalCamera=true;gm.fixtureCameraTarget=null;
        int noTarget=fixture_camera_open(52,0,0);
        Check("camera_null_target_refused",noTarget==0 && gm.ManualCameraTargetControl==null,noTarget);
        gm.fixtureCameraTarget=target.transform;
        int opened=fixture_camera_open(52,0,1);
        Check("camera_open_own",opened==0 && gm.ManualCameraTargetControl!=null,opened);

        ulong before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        int state=fixture_camera_state(1,0,52,10,12,before);
        Check("camera_own_effect",At(target.transform,10,12,-17) && state==0,state);
        local.value=new float2(14,16);before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        state=fixture_camera_state(1,0,52,14,16,before);
        Check("camera_own_updates",At(target.transform,14,16,-17) && state==0,state);
        int mode=fixture_camera_mode(1);before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        state=fixture_camera_state(1,0,-99,8,10,before);
        Check("camera_shared_centroid",mode==0 && state==0 && At(target.transform,8,10,-17),mode!=0?mode:state);
        first.dead=true;before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        state=fixture_camera_state(1,0,-99,14,16,before);
        Check("camera_shared_dead_first",state==0 && At(target.transform,14,16,-17),state);
        first.dead=false;local.dead=true;before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        state=fixture_camera_state(1,1,-99,2,4,before);
        Check("camera_shared_dead_second",state==0 && At(target.transform,2,4,-17),state);
        first.dead=true;before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        state=fixture_camera_state(0,0,-99,0,0,before);
        Check("camera_all_dead",state==0 && At(target.transform,2,4,-17),state);
        local.dead=false;mode=fixture_camera_mode(0);before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        state=fixture_camera_state(1,0,52,14,16,before);
        Check("camera_revival_own",mode==0 && state==0 && At(target.transform,14,16,-17),mode!=0?mode:state);
        first.dead=false;local.dead=true;before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        state=fixture_camera_state(1,1,41,2,4,before);
        Check("camera_spectate_living",state==0 && At(target.transform,2,4,-17),state);
        gm.players.Insert(0,third);before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        state=fixture_camera_state(1,1,41,2,4,before);
        Check("camera_spectate_stable",state==0 && At(target.transform,2,4,-17),state);
        int wrong=fixture_camera_wrong_thread();local.dead=false;before=fixture_camera_updates();gm.FixtureInvokeCameraTarget();
        state=fixture_camera_state(1,0,52,14,16,before);
        Check("camera_wrong_thread",wrong==0 && state==0 && At(target.transform,14,16,-17),wrong!=0?wrong:state);

        // A cutscene/additional camera target must get stock aggregation back,
        // not merely an installed native callback that silently does nothing.
        target.transform.position=new Vector3(81,82,-17);
        gm.fixtureNormalCamera=false;before=fixture_camera_updates();
        bool normalCallbackPresent=gm.ManualCameraTargetControl!=null;
        gm.FixtureInvokeCameraTarget();
        Check("camera_non_normal_restores_stock",normalCallbackPresent && gm.ManualCameraTargetControl==null &&
            At(target.transform,81,82,-17) && fixture_camera_updates()==before);
        gm.fixtureNormalCamera=true;
        int normalClosed=fixture_camera_close();
        int normalInit=fixture_camera_init(managed,os);
        int normalOpen=fixture_camera_open(52,0,1);

        // Replacing a stage must never mutate the old target using stale run data.
        var originalStage=gm.stage;gm.stage=new Stage();target.transform.position=new Vector3(31,32,-17);
        bool callbackPresent=gm.ManualCameraTargetControl!=null;gm.FixtureInvokeCameraTarget();
        Check("camera_stage_replacement",normalClosed==0 && normalInit==0 && normalOpen==0 && callbackPresent && At(target.transform,31,32,-17));gm.stage=originalStage;
        // Stage refusal removes our callback. Open an independent valid
        // session so this next case really executes the target-change path.
        int stageClosed=fixture_camera_close();
        int targetInit=fixture_camera_init(managed,os);
        int targetOpen=fixture_camera_open(52,0,1);
        // A target replacement may be refused or adopted, but the old object must never be touched.
        target.transform.position=new Vector3(41,42,-17);replacement.transform.position=new Vector3(51,52,-23);
        gm.fixtureCameraTarget=replacement.transform;callbackPresent=gm.ManualCameraTargetControl!=null;gm.FixtureInvokeCameraTarget();
        Check("camera_target_replacement",stageClosed==0 && targetInit==0 && targetOpen==0 && callbackPresent && At(target.transform,41,42,-17) &&
            (At(replacement.transform,51,52,-23)||At(replacement.transform,14,16,-23)));
        int closed=fixture_camera_close();
        Check("camera_close_owned",closed==0 && gm.ManualCameraTargetControl==null,closed);

        // A foreign replacement belongs to its installer, even if close refuses.
        gm.fixtureCameraTarget=target.transform;gm.ManualCameraTargetControl=null;
        init=fixture_camera_init(managed,os);opened=fixture_camera_open(52,0,1);
        gm.ManualCameraTargetControl=foreign;closed=fixture_camera_close();gm.FixtureInvokeCameraTarget();
        Check("camera_foreign_replacement_preserved",init==0 && opened==0 && ReferenceEquals(gm.ManualCameraTargetControl,foreign) && foreignCalls==1);
        // If close refuses, retain the owner; never destroy a callback root unsafely.
        gm.ManualCameraTargetControl=null;
        if(closed!=0) fixture_camera_close();

        // A retained old Action must not dispatch into a later native owner.
        init=fixture_camera_init(managed,os);opened=fixture_camera_open(52,0,1);
        var staleAction=gm.ManualCameraTargetControl;
        int firstClose=fixture_camera_close();
        int laterInit=fixture_camera_init(managed,os);
        int laterOpen=fixture_camera_open(52,0,1);
        target.transform.position=new Vector3(61,62,-17);
        before=fixture_camera_updates();staleAction?.Invoke();
        bool staleIgnored=At(target.transform,61,62,-17) && fixture_camera_updates()==before;
        gm.FixtureInvokeCameraTarget();
        Check("camera_late_delegate_isolated",init==0 && opened==0 && firstClose==0 && laterInit==0 && laterOpen==0 &&
            staleAction!=null && staleIgnored && At(target.transform,14,16,-17) && fixture_camera_updates()==before+1);
        fixture_camera_close();

        // Actually invoke the native Action from a worker, not merely its
        // mode API. A safe close/reopen must not inherit that old diagnostic.
        init=fixture_camera_init(managed,os);opened=fixture_camera_open(52,0,1);
        var workerAction=gm.ManualCameraTargetControl;
        int initiallyHealthy=fixture_camera_diagnostic(0);
        target.transform.position=new Vector3(91,92,-17);before=fixture_camera_updates();
        Exception workerFailure=null;
        var worker=new System.Threading.Thread(()=>{
            try { workerAction?.Invoke(); } catch(Exception error) { workerFailure=error; }
        });
        worker.Start();bool workerJoined=worker.Join(5000);
        int foreignDiagnostic=fixture_camera_diagnostic(1);
        bool foreignDidNotWrite=At(target.transform,91,92,-17) && fixture_camera_updates()==before;
        int reusableClose=fixture_camera_close_keep();
        int reusableOpen=fixture_camera_open(52,0,1);
        gm.FixtureInvokeCameraTarget();
        int nextDiagnostic=fixture_camera_diagnostic(0);
        Check("camera_foreign_callback_generation_reset",init==0 && opened==0 && workerAction!=null && initiallyHealthy==0 &&
            workerJoined && workerFailure==null && foreignDiagnostic==0 && foreignDidNotWrite && reusableClose==0 &&
            reusableOpen==0 && nextDiagnostic==0 && At(target.transform,14,16,-17),nextDiagnostic);
        fixture_camera_close();

        // Unity native destruction leaves a rooted managed wrapper. Cleanup
        // must safely remove only its managed callback and admit the next run.
        init=fixture_camera_init(managed,os);opened=fixture_camera_open(52,0,1);
        var destroyedAction=gm.ManualCameraTargetControl;
        gm.FixtureInvokeCameraTarget();
        bool oldWasActive=fixture_camera_active(1)==0 && At(target.transform,14,16,-17);
        UnityEngine.Object.DestroyImmediate(gm.gameObject);
        gm=new GameObject("ReplacementGameManager").AddComponent<GameManager>();
        gm.running=true;gm.stage=originalStage;
        gm.players=new System.Collections.Generic.List<Character>{first,local};
        gm.fixtureCameraTarget=target.transform;gm.fixtureNormalCamera=true;
        // Do not invoke the old Action or close first: this is the exact
        // observation made by the UI loop when the old scene stops ticking.
        int replacementState=fixture_camera_active(0);
        Check("camera_replaced_manager_inactive",oldWasActive && replacementState==0,replacementState);
        int destroyedClose=fixture_camera_close();
        int replacementInit=destroyedClose==0?fixture_camera_init(managed,os):-1;
        int replacementOpen=fixture_camera_open(52,0,1);
        target.transform.position=new Vector3(71,72,-17);
        before=fixture_camera_updates();destroyedAction?.Invoke();
        bool destroyedIgnored=At(target.transform,71,72,-17) && fixture_camera_updates()==before;
        gm.FixtureInvokeCameraTarget();
        Check("camera_destroyed_manager_recovers",init==0 && opened==0 && destroyedClose==0 && replacementInit==0 && replacementOpen==0 &&
            destroyedAction!=null && destroyedIgnored && At(target.transform,14,16,-17) && fixture_camera_updates()==before+1);
        fixture_camera_close();
        gm.fixtureCameraTarget=null;
        UnityEngine.Object.DestroyImmediate(target);UnityEngine.Object.DestroyImmediate(replacement);
        return gm;
    }
}
