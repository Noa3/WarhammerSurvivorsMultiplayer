using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using Rewired;
using UnityEngine;
using VampireSurvivors.Framework;

public sealed class CoopRosterFixture : MonoBehaviour {
    [DllImport("coop_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_open(uint os, int managed);
    [DllImport("coop_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_inspect(int[] configured, out int configuredCount, int[] enrolled, out int enrolledCount);
    [DllImport("coop_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_enroll(int id);
    [DllImport("coop_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_select(int id);
    [DllImport("coop_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_copy_character(int id);
    [DllImport("coop_fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_rollback();
    [DllImport("coop_fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_close();
    [DllImport("coop_fixture", CallingConvention = CallingConvention.Cdecl)] static extern IntPtr fixture_reason();
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    int failures;
    Player original, target;
    MultiplayerManager manager;
    uint os;
    int managed;

    void Check(string name, bool ok) {
        var reason = fixture_reason();
        Debug.Log("COOP_ROSTER_TEST " + name + "=" + (ok ? "PASS" : "FAIL " + (reason == IntPtr.Zero ? "" : Marshal.PtrToStringAnsi(reason))));
        if (!ok) ++failures;
    }
    void Setup() {
        original = new Player(0); target = new Player(41);
        ReInput.players = new ReInput.PlayerHelper();
        ReInput.players.configured.Add(original); ReInput.players.configured.Add(target);
        manager = new MultiplayerManager(); manager.roster.Add(original); MultiplayerManager.Instance = manager;
        manager._playerOptions.Config.MultiplayerSelections[original] = new CoopSelections();
        if (fixture_open(os, managed) != 0) throw new Exception("fixture_open: " + Marshal.PtrToStringAnsi(fixture_reason()));
    }
    bool Baseline() { return manager.roster.Count == 1 && ReferenceEquals(manager.roster[0], original); }
    bool Added() { return manager.roster.Count == 2 && ReferenceEquals(manager.roster[0], original) && ReferenceEquals(manager.roster[1], target); }
    void Awake() {
        os = GetCurrentThreadId(); managed = Thread.CurrentThread.ManagedThreadId;
        try { Run(); } catch (Exception ex) { ++failures; Debug.LogException(ex); }
        finally { fixture_close(); }
        Debug.Log("COOP_ROSTER_TEST RESULT failures=" + failures);
        Application.Quit(failures == 0 ? 0 : 1);
    }
    void Run() {
        Setup();
        var configured = new int[16]; var enrolled = new int[16]; int nc, ne;
        int status = fixture_inspect(configured, out nc, enrolled, out ne);
        Check("inspect", status == 0 && configured.Take(nc).SequenceEqual(new[] { 0, 41 }) && enrolled.Take(ne).SequenceEqual(new[] { 0 }));
        Check("unknown", fixture_enroll(99) != 0 && manager.addCalls == 0 && Baseline());
        Check("already", fixture_enroll(0) != 0 && manager.addCalls == 0 && Baseline());
        Check("enroll", fixture_enroll(41) == 0 && manager.addCalls == 1 && Added());
        var config = manager._playerOptions.Config;
        Check("copy_unknown", fixture_copy_character(99) != 0 && config.MultiplayerSelections[original].SelectedCharacter == VampireSurvivors.Data.CharacterType.VOID && config.MultiplayerSelections[target].SelectedCharacter == VampireSurvivors.Data.CharacterType.VOID);
        Check("copy_original", fixture_copy_character(0) == 0 && config.MultiplayerSelections[original].SelectedCharacter == VampireSurvivors.Data.CharacterType.FixtureAlpha && config.MultiplayerSelections[target].SelectedCharacter == VampireSurvivors.Data.CharacterType.VOID);
        config.SelectedCharacter = VampireSurvivors.Data.CharacterType.FixtureBeta;
        Check("copy_target", fixture_copy_character(41) == 0 && config.MultiplayerSelections[target].SelectedCharacter == VampireSurvivors.Data.CharacterType.FixtureBeta && config.MultiplayerSelections[original].SelectedCharacter == VampireSurvivors.Data.CharacterType.FixtureAlpha && config.SelectedCharacter == VampireSurvivors.Data.CharacterType.FixtureBeta);
        config.SelectedCharacter = VampireSurvivors.Data.CharacterType.VOID;
        Check("copy_unbought", fixture_copy_character(41) != 0 && config.MultiplayerSelections[target].SelectedCharacter == VampireSurvivors.Data.CharacterType.FixtureBeta);
        config.SelectedCharacter = VampireSurvivors.Data.CharacterType.FixtureBeta;
        Check("repeat", fixture_enroll(41) != 0 && manager.addCalls == 1 && Added());
        Check("select_unknown", fixture_select(99) != 0 && manager.selectCalls == 0 && Added());
        Check("select_target", fixture_select(41) == 0 && manager.selectCalls == 1 && manager.GetSelectedPlayerIndex() == 1 && ReferenceEquals(manager.GetSelectedPlayer(), target) && Added());
        Check("select_original", fixture_select(0) == 0 && manager.selectCalls == 2 && manager.GetSelectedPlayerIndex() == 0 && ReferenceEquals(manager.GetSelectedPlayer(), original) && Added());
        Check("rollback", fixture_rollback() == 0 && manager.removeCalls == 1 && Baseline());
        fixture_close();

        Setup(); fixture_close(); fixture_open(os + 1, managed);
        Check("wrong_os", fixture_enroll(41) != 0 && manager.addCalls == 0 && Baseline()); fixture_close();
        Setup(); fixture_close(); fixture_open(os, managed + 1);
        Check("wrong_managed", fixture_enroll(41) != 0 && manager.addCalls == 0 && Baseline()); fixture_close();
        Setup(); var context = SynchronizationContext.Current;
        try { SynchronizationContext.SetSynchronizationContext(null); Check("null_context", fixture_enroll(41) != 0 && manager.addCalls == 0 && Baseline()); }
        finally { SynchronizationContext.SetSynchronizationContext(context); }
        fixture_close();

        Setup(); manager.addFault = 1;
        Check("add_before", fixture_enroll(41) != 0 && Baseline() && fixture_rollback() == 0 && Baseline()); fixture_close();
        Setup(); manager.addFault = 2;
        Check("add_after", fixture_enroll(41) != 0 && Added() && fixture_rollback() == 0 && Baseline()); fixture_close();
        Setup(); bool added = fixture_enroll(41) == 0 && Added(); manager.removeFault = true;
        bool failed = fixture_rollback() != 0 && Added(); manager.removeFault = false;
        Check("remove_retry", added && failed && fixture_rollback() == 0 && Baseline() && manager.removeCalls == 2); fixture_close();
        Setup(); added = fixture_enroll(41) == 0 && Added();
        var newManager = new MultiplayerManager(); newManager.roster.Add(original); MultiplayerManager.Instance = newManager;
        Check("manager_replaced", added && fixture_rollback() != 0 && newManager.removeCalls == 0 && newManager.roster.Count == 1 && ReferenceEquals(newManager.roster[0], original) && Added());
        MultiplayerManager.Instance = manager;
        if (fixture_rollback() != 0 || !Baseline()) throw new Exception("fixture cleanup rollback failed");
    }
}
