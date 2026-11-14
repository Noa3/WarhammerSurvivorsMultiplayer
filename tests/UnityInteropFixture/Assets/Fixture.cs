using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Threading; using UnityEngine; using UnityEngine.Scripting;
[Preserve] public sealed class FixtureItem { public int Value {get; private set;} public FixtureItem(int v){Value=v;} }
[Preserve] public sealed class Fixture : MonoBehaviour {
 [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_test(int managedId, uint osId, int mode);
 [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
 [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_production_guard(int managedId, uint osId);
 [Preserve] public static Fixture Instance; [Preserve] public static Transform GetFixtureTransform()=>Instance.transform; [Preserve] public static List<FixtureItem> GetFixtureItems()=>Instance.items;
 int failures; int frames; float start; string status="Starting"; List<FixtureItem> items;
 void Check(string name,bool ok){Debug.Log("FIXTURE "+name+"="+(ok?"PASS":"FAIL"));if(!ok) failures++;}
 void Awake(){try { AwakeCore(); } catch(Exception ex) { failures++; status="EXCEPTION: "+ex.Message; Debug.LogError("FIXTURE awake exception "+ex); }}
 void AwakeCore(){Instance=this; start=Time.realtimeSinceStartup; items=new List<FixtureItem>(); for(int i=0;i<23;i++)items.Add(new FixtureItem(i)); transform.position=new Vector3(1.25f,2.5f,3.75f);
  int id=Thread.CurrentThread.ManagedThreadId; uint os=GetCurrentThreadId(); Debug.Log("FIXTURE awake managed="+id+" os="+os+" context="+SynchronizationContext.Current?.GetType().FullName);
  Check("production_main_thread_guard", fixture_production_guard(id,os)==0);
  Check("production_wrong_os", fixture_production_guard(id,os+1)!=0);
  Check("production_wrong_managed", fixture_production_guard(id+1,os)!=0);
  Check("positive", fixture_test(id,os,0)==0);
  Check("mismatched_os", fixture_test(id,os+1,0)==23);
  Check("mismatched_managed", fixture_test(id+1,os,0)==26);
  Check("missing_method", fixture_test(id,os,1)==21);
  Check("ambiguous_method", fixture_test(id,os,2)==22);
  var old=SynchronizationContext.Current; SynchronizationContext.SetSynchronizationContext(null); Check("production_null_context",fixture_production_guard(id,os)!=0); Check("null_context",fixture_test(id,os,0)==24); SynchronizationContext.SetSynchronizationContext(new SynchronizationContext()); Check("production_substituted_context",fixture_production_guard(id,os)!=0); Check("substituted_context",fixture_test(id,os,0)==24); SynchronizationContext.SetSynchronizationContext(old);
  int worker=0; var t=new Thread(()=>{worker=fixture_test(id,os,0);});t.Start();t.Join();Check("wrong_thread",worker==23);
  if(Array.IndexOf(Environment.GetCommandLineArgs(),"--fixture-force-fail")>=0) Check("forced_failure",false);
  status=failures==0?"All Awake checks passed":"FAILURES: "+failures;
 }
 void Update(){frames++; if(frames==2){var id=Thread.CurrentThread.ManagedThreadId;try { Check("update_positive",fixture_test(id,GetCurrentThreadId(),0)==0); } catch(Exception ex) { failures++; Debug.LogError("FIXTURE update exception "+ex); } status=failures==0?"Interop checks passed (Awake + Update)":"FAILURES: "+failures;}
  if(Environment.GetCommandLineArgs().Length>0 && Array.IndexOf(Environment.GetCommandLineArgs(),"--fixture-test")>=0 && frames>=10){Debug.Log("FIXTURE RESULT failures="+failures+" frames="+frames);Application.Quit(failures==0?0:1);}
  if(Time.realtimeSinceStartup-start>60){Debug.Log("FIXTURE timeout/result failures="+failures+" frames="+frames);Application.Quit(failures==0?0:1);}
 }
 void OnGUI(){GUI.Box(new Rect(20,20,560,120),"Original Unity IL2CPP interop fixture"); GUI.Label(new Rect(40,55,500,30),status); GUI.Label(new Rect(40,85,500,30),"Live Update frame: "+frames);}
}