using System;
using System.Runtime.InteropServices;
using System.Threading;
using Steamworks;
using UnityEngine;
public sealed class SteamPersonaFixture : MonoBehaviour {
    [DllImport("steam_fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_open(uint os,int managed,int hide);
    [DllImport("steam_fixture",CallingConvention=CallingConvention.Cdecl)] static extern int fixture_read(int status,[MarshalAs(UnmanagedType.LPUTF8Str)] string expected);
    [DllImport("steam_fixture",CallingConvention=CallingConvention.Cdecl)] static extern void fixture_close();
    [DllImport("steam_fixture",CallingConvention=CallingConvention.Cdecl)] static extern IntPtr fixture_reason();
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    int failures; uint os; int managed;
    void Check(string name,bool ok) { Debug.Log("STEAM_TEST "+name+"="+(ok?"PASS":"FAIL "+Marshal.PtrToStringAnsi(fixture_reason()))); if(!ok)++failures; }
    bool Read(int status,string value=null) { return fixture_read(status,value)==0; }
    void Awake() {
        os=GetCurrentThreadId();managed=Thread.CurrentThread.ManagedThreadId;
        try { Run(); } catch(Exception e) {++failures;Debug.LogException(e);} finally {fixture_close();}
        Debug.Log("STEAM_TEST RESULT failures="+failures);Application.Quit(failures==0?0:1);
    }
    void Run() {
        Check("missing_metadata",fixture_open(os,managed,1)==2);
        Check("initialize",fixture_open(os,managed,0)==0);
        SteamAPI.running=false; SteamAPI.user=0;SteamFriends.calls=0;
        Check("steam_not_running",Read(1)&&SteamFriends.calls==0);
        SteamAPI.running=true;
        Check("uninitialized_handle",Read(1)&&SteamFriends.calls==0);
        SteamAPI.user=37;SteamFriends.name="  Alice  ";
        Check("initialized",Read(0,"Alice"));
        SteamFriends.name=" \u00e9\u4e16\U0001f680\u2003";
        Check("unicode_trim",Read(0,"\u00e9\u4e16\U0001f680"));
        SteamFriends.name=" \t\n";Check("whitespace_only",Read(1));
        SteamFriends.name=null;Check("null_name",Read(1));
        SteamFriends.name=new string(new[] { (char)0xd800 });Check("malformed_utf16",Read(2));
        SteamFriends.name=new string('x',1025);Check("oversized",Read(2));
        SteamFriends.name="x\0y";Check("embedded_nul",Read(2));
        SteamFriends.name="Alice";
        SteamAPI.fault=1;Check("running_exception",Read(2));
        SteamAPI.fault=2;Check("handle_exception",Read(2));
        SteamAPI.fault=3;Check("conversion_exception",Read(2));
        SteamAPI.fault=4;Check("persona_exception",Read(2));SteamAPI.fault=0;
        int before=SteamFriends.calls;int worker=-1;var t=new Thread(()=>worker=fixture_read(2,null));t.Start();t.Join();
        Check("wrong_thread",worker==0&&SteamFriends.calls==before);
        var context=SynchronizationContext.Current;
        try {SynchronizationContext.SetSynchronizationContext(null);Check("null_context",Read(2));} finally {SynchronizationContext.SetSynchronizationContext(context);}
        bool repeated=true;for(int i=0;i<100;++i){GC.Collect();GC.WaitForPendingFinalizers();repeated &= Read(0,"Alice");}
        Check("repeated_gc",repeated);
        fixture_close();Check("wrong_os_init",fixture_open(os+1,managed,0)==0&&Read(2));
        fixture_close();Check("wrong_managed_init",fixture_open(os,managed+1,0)==0&&Read(2));
    }
}
