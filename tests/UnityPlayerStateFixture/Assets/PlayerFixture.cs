using System;
using System.Runtime.InteropServices;
using System.Threading;
using UnityEngine;
using VampireSurvivors.Framework;
using VampireSurvivors.Objects;
using CharacterController = VampireSurvivors.Objects.Characters.CharacterController;
using VampireSurvivors.Data;
using Unity.Mathematics;
public sealed class PlayerFixture : MonoBehaviour {
 [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_test(int managed,uint os,int mode);
 [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_upgrade(int managed,uint os,int mode);
 [DllImport("fixture",CallingConvention=CallingConvention.Cdecl)] static extern IntPtr fixture_reason();
 [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
 int failures;
 string Reason(int code){if(code==0)return "";var p=fixture_reason();return " reason="+(p==IntPtr.Zero?"-":Marshal.PtrToStringAnsi(p));}
 void Check(string label,int actual){Debug.Log("PLAYER_TEST "+label+"="+(actual==0?"PASS":"FAIL "+actual+Reason(actual)));if(actual!=0)++failures;}
 void Awake(){try {Run();}catch(Exception ex){++failures;Debug.LogException(ex);} Debug.Log("PLAYER_TEST RESULT failures="+failures);Application.Quit(failures==0?0:1);}
 void Run(){
  int managed=Thread.CurrentThread.ManagedThreadId;uint os=GetCurrentThreadId();Debug.Log("PLAYER_TEST identity="+managed+"/"+os+" context="+SynchronizationContext.Current?.GetType().FullName);
  // What the runtime actually hands back for the declared Object[] signature.
  Debug.Log("PLAYER_TEST manager_lookup_type="+UnityEngine.Object.FindObjectsOfType(typeof(GameManager)).GetType().FullName+" length="+UnityEngine.Object.FindObjectsOfType(typeof(GameManager)).Length);
  Check("no_manager",fixture_test(managed,os,1));
  var gm=new GameObject("GameManager").AddComponent<GameManager>();
  Check("null_stage",fixture_test(managed,os,1));gm.stage=new Stage();Check("empty_players",fixture_test(managed,os,1));
  gm.players.Add(new CharacterController {value=new float2(1.25f,-2.5f),hp=83.5f,xp=12.25f,dead=false,level=5,kind=CharacterType.A,input=new Rewired.Player(41)});
  gm.players.Add(new CharacterController {value=new float2(-9f,6.5f),hp=0f,xp=77f,dead=true,level=8,kind=CharacterType.B,input=new Rewired.Player(52)});
  // Managed view of the same contract: keeps List<T>.Count/get_Item and the
  // getters referenced from managed code, and records the expected values.
  Debug.Log("PLAYER_TEST managed_view count="+gm.players.Count+" first="+gm.players[0].value.x+","+gm.players[0].value.y+" last_dead="+gm.players[gm.players.Count-1].IsDead);
  Check("two_players",fixture_test(managed,os,0));
  gm.running=false;Check("populated_stopped",fixture_test(managed,os,1));gm.running=true;
  Check("stage_root_failure",fixture_test(managed,os,3));Check("players_root_failure",fixture_test(managed,os,4));
  Check("wrong_native_thread",fixture_test(managed,os+1,2));Check("wrong_managed_thread",fixture_test(managed+1,os,2));
  var ctx=SynchronizationContext.Current;SynchronizationContext.SetSynchronizationContext(null);Check("null_context",fixture_test(managed,os,2));SynchronizationContext.SetSynchronizationContext(new SynchronizationContext());Check("substituted_context",fixture_test(managed,os,2));SynchronizationContext.SetSynchronizationContext(ctx);
  int worker=-1;var thread=new Thread(()=>{worker=fixture_test(managed,os,2);});thread.Start();thread.Join();Check("worker_thread",worker);
  gm.players.Add(gm.players[0]);gm.players.Add(gm.players[0]);gm.players.Add(gm.players[0]);Check("over_cap",fixture_test(managed,os,2));gm.players.RemoveRange(2,3);
  gm.players[0].hp=float.NaN;Check("non_finite",fixture_test(managed,os,2));gm.players[0].hp=83.5f;
  gm.players[1].hp=float.NaN;Check("late_non_finite",fixture_test(managed,os,2));gm.players[1].hp=0f;
  gm.players[0].input=null;Check("null_input",fixture_test(managed,os,2));gm.players[0].input=new Rewired.Player(41);
  Check("recovered",fixture_test(managed,os,0));
  gm.players[0].ConfigureManagers(new CharacterWeaponsManager(),null);
  var accessories=new CharacterAccessoriesManager(); gm.players[1].ConfigureManagers(null,accessories);
  Check("upgrade_missing_page",fixture_upgrade(managed,os,1));
  var page=new GameObject("UpgradePage").AddComponent<Wilson.UI.WSLevelUpPage>();
  var session=new GameSessionData {ActiveCharacter=gm.players[1]}; var factory=new LevelUpFactory();page.Configure(session,factory);
  Check("upgrade_second_actor",fixture_upgrade(managed,os,0));
  Check("upgrade_wrong_collection_type",fixture_upgrade(managed,os,14));
  page.Configure(null,factory);Check("upgrade_null_session",fixture_upgrade(managed,os,2));page.Configure(session,factory);
  session.ActiveCharacter=null;Check("upgrade_null_active",fixture_upgrade(managed,os,3));
  session.ActiveCharacter=new CharacterController();Check("upgrade_foreign_active",fixture_upgrade(managed,os,4));session.ActiveCharacter=gm.players[1];
  accessories.ActiveEquipment=null;Check("upgrade_null_collection",fixture_upgrade(managed,os,5));accessories.ActiveEquipment=new System.Collections.Generic.List<Equipment>();
  page.Configure(session,null);Check("upgrade_null_factory",fixture_upgrade(managed,os,6));page.Configure(session,factory);
  var duplicate=new GameObject("DuplicatePage").AddComponent<Wilson.UI.WSLevelUpPage>();Check("upgrade_duplicate_pages",fixture_upgrade(managed,os,7));DestroyImmediate(duplicate.gameObject);
  Check("upgrade_wrong_thread",fixture_upgrade(managed,os+1,8));
  gm.running=false;Check("upgrade_stopped",fixture_upgrade(managed,os,9));gm.running=true;
  gm.players[1].input=null;Check("upgrade_late_error",fixture_upgrade(managed,os,10));gm.players[1].input=new Rewired.Player(52);
  Check("upgrade_stage_root_failure",fixture_upgrade(managed,os,11));
  var second=gm.players[1];gm.players[1]=gm.players[0];Check("upgrade_duplicate_actor",fixture_upgrade(managed,os,13));gm.players[1]=second;
  Check("upgrade_recovered",fixture_upgrade(managed,os,0));
  DestroyImmediate(gm.gameObject);Check("upgrade_no_manager",fixture_upgrade(managed,os,12));
 }
}
