using System;
using System.Collections;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using UnityEngine;
using VampireSurvivors.Data;
using VampireSurvivors.Framework;
using VampireSurvivors.Objects;
using Actor = VampireSurvivors.Objects.Characters.CharacterController;

public sealed class InputDriverFixture : MonoBehaviour
{
    [DllImport("kernel32")] static extern uint GetCurrentThreadId();
    [DllImport("kernel32")] static extern uint GetCurrentProcessId();
    [DllImport("user32")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_drive_start(int managed, uint os, int mode);
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_drive_submit(ulong epoch, ulong sequence, float x, float y, uint duration);
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_drive_stop();
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_drive_force_pump_stop();
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_drive_drop_worker();
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern ulong fixture_drive_read(int field);
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern IntPtr fixture_drive_reason();
    int failures;
    Rewired.Player primary, remote;
    Rewired.ControllerWithAxes keyboard;
    Rewired.ControllerMap originalMap;
    int StartDrive(int mode = 0)
    {
        int result = fixture_drive_start(Thread.CurrentThread.ManagedThreadId, GetCurrentThreadId(), mode);
        if (result != 0) Debug.Log("DRIVE_REASON " + Marshal.PtrToStringAnsi(fixture_drive_reason()));
        return result;
    }
    void Check(string name, bool passed)
    {
        Debug.Log("DRIVE_TEST " + name + "=" + (passed ? "PASS" : "FAIL"));
        if (!passed) ++failures;
    }
    IEnumerator Until(Func<bool> condition)
    {
        float end = Time.realtimeSinceStartup + 6;
        while (!condition() && Time.realtimeSinceStartup < end) yield return null;
    }
    static bool OwnOsForeground()
    {
        var window = GetForegroundWindow();
        return window != IntPtr.Zero && GetWindowThreadProcessId(window, out uint process) != 0 && process == GetCurrentProcessId();
    }
    bool ResourcesGone() => Rewired.ReInput.controllerHelper.values.Count == 0 && !remote.controllers.Controllers.Any();
    bool PrimaryUntouched() => primary.controllers.Controllers.Count() == 1 && primary.controllers.ContainsController(keyboard) &&
        primary.controllers.maps.GetMaps(keyboard).Count == 1 && ReferenceEquals(primary.controllers.maps.GetMaps(keyboard)[0], originalMap) &&
        originalMap.enabled && primary.GetAxis("Move Horizontal") == 0 && primary.GetAxis("Move Vertical") == 0;
    IEnumerator CleanupFixture()
    {
        fixture_drive_stop();
        fixture_drive_force_pump_stop();
        yield return Until(() => fixture_drive_read(4) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);
    }
    IEnumerator Start()
    {
        Application.runInBackground = true;
        var input = new GameObject("DriverInputManager").AddComponent<Rewired.FixtureInputManager>();
        var data = new Rewired.Data.UserData { ids = new[] { 7 } };
        data.definitions.Add(7, new Rewired.Data.CustomController_Editor { axisIds = new[] { 2, 9 } });
        input.Configure(data);
        var manager = new GameObject("DriverGameManager").AddComponent<GameManager>();
        manager.stage = new Stage(); manager.running = true;
        primary = new Rewired.Player(0); remote = new Rewired.Player(1);
        manager.players.Add(new Actor { input = primary, hp = 100, level = 1, kind = CharacterType.A, value = new Unity.Mathematics.float2(1, 0) });
        manager.players.Add(new Actor { input = remote, hp = 100, level = 1, kind = CharacterType.B, value = new Unity.Mathematics.float2(2, 0) });
        Rewired.ReInput.playerHelper.values.Add(0, primary); Rewired.ReInput.playerHelper.values.Add(1, remote);
        keyboard = new Rewired.ControllerWithAxes(10, Rewired.ControllerType.Keyboard, new[] { 2, 9 });
        primary.controllers.AddController(keyboard, false);
        originalMap = new Rewired.ControllerMap(keyboard, 17, 23);
        originalMap.CreateElementMap(Rewired.ElementAssignment.FullAxisAssignment(2, 101, false));
        originalMap.CreateElementMap(Rewired.ElementAssignment.FullAxisAssignment(9, 103, false));
        primary.controllers.maps.AddMap(keyboard, originalMap, true);
        yield return null;
        // Keep setup readiness separate from the required real OS-focus interval.
        // Automated operators can focus this exact owned player during this hold.
        Debug.Log("DRIVE_FIXTURE_READY_FOR_FOCUS");
        float focusDeadline = Time.realtimeSinceStartup + 60;
        while (!OwnOsForeground() && Time.realtimeSinceStartup < focusDeadline) yield return null;
        int opened = StartDrive();
        Check("drive_real_controller_start", opened == 0 && Rewired.ReInput.FixtureInputSubscribers == 1 && Rewired.ReInput.controllerHelper.values.Count == 1);
        Check("drive_actual_owned_window_focus", fixture_drive_read(8) == 1);
        Check("drive_actual_mailbox_submit", fixture_drive_submit(1, 1, 0.75f, -0.25f, 120) == 1);
        Rewired.ReInput.FixtureFireInput();
        Check("drive_event_updates_actual_remote_axes", remote.GetAxis("Move Horizontal") == 0.75f && remote.GetAxis("Move Vertical") == -0.25f && fixture_drive_read(3) == 1);
        Check("drive_primary_resources_and_axes_unchanged", PrimaryUntouched());
        yield return Until(() => remote.GetAxis("Move Horizontal") == 0 && remote.GetAxis("Move Vertical") == 0);
        Check("drive_actual_lease_expires_without_input_events", remote.GetAxis("Move Horizontal") == 0 && remote.GetAxis("Move Vertical") == 0 && fixture_drive_read(3) == 1 && Rewired.ReInput.controllerHelper.values.Count == 1);
        Check("drive_replay_and_wrong_epoch_refused", fixture_drive_submit(1, 1, 1, 0, 100) == 0 && fixture_drive_submit(2, 2, 1, 0, 100) == 0);
        fixture_drive_stop();
        yield return Until(() => fixture_drive_read(4) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("drive_producer_stop_removes_controller", fixture_drive_read(4) == 1 && ResourcesGone());
        Check("drive_completed_client_unsubscribes", Rewired.ReInput.FixtureInputSubscribers == 0 && fixture_drive_read(5) == 1);
        yield return CleanupFixture();

        Check("drive_start_before_faulted_owner_drop", StartDrive() == 0);
        Check("drive_live_before_faulted_owner_drop", fixture_drive_submit(1, 1, 0.5f, 0, 250) == 1);
        Rewired.ReInput.FixtureFireInput();
        Check("drive_nonzero_before_cleanup_fault", remote.GetAxis("Move Horizontal") == 0.5f);
        Rewired.CustomController.FailZero = true;
        Rewired.Player.ControllerHelper.MapHelper.FailRemove = true;
        fixture_drive_drop_worker();
        yield return Until(() => fixture_drive_read(1) == 1);
        Check("drive_actual_cleanup_failure_retains_owner", fixture_drive_read(1) == 1 && fixture_drive_read(4) == 0 && fixture_drive_read(7) == 1 && Rewired.ReInput.controllerHelper.values.Count == 1 && remote.GetAxis("Move Horizontal") == 0.5f);
        Check("drive_stop_blocks_new_packets", fixture_drive_submit(1, 2, 1, 0, 100) == 0);
        Rewired.CustomController.FailZero = false;
        Rewired.Player.ControllerHelper.MapHelper.FailRemove = false;
        yield return Until(() => fixture_drive_read(4) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("drive_actual_cleanup_retries_after_wrapper_gone", fixture_drive_read(4) == 1 && ResourcesGone() && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("drive_cleanup_preserves_primary", PrimaryUntouched());

        int invalid = StartDrive(1);
        yield return Until(() => fixture_drive_read(4) == 1);
        Check("drive_failed_open_is_retained_then_cleaned", invalid != 0 && fixture_drive_read(4) == 1 && ResourcesGone() && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("drive_start_before_run_end", StartDrive() == 0);
        manager.running = false;
        yield return Until(() => fixture_drive_read(4) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("drive_run_end_independent_controller_cleanup", fixture_drive_read(4) == 1 && ResourcesGone());
        Check("drive_run_end_removes_subscription", Rewired.ReInput.FixtureInputSubscribers == 0);
        yield return CleanupFixture();
        manager.running = true;
        Check("drive_start_before_actor_death", StartDrive() == 0);
        manager.players[1].dead = true;
        yield return Until(() => fixture_drive_read(4) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("drive_dead_actor_independent_controller_cleanup", fixture_drive_read(4) == 1 && ResourcesGone());
        Check("drive_dead_actor_removes_subscription", Rewired.ReInput.FixtureInputSubscribers == 0);
        yield return CleanupFixture();
        Check("drive_final_resources_and_primary", ResourcesGone() && PrimaryUntouched() && Rewired.ReInput.FixtureInputSubscribers == 0);
        // Restore the otherwise valid run so unrelated lifecycle refusal cannot
        // mask context validation. Every mismatch must refuse before ownership.
        manager.players[1].dead = false;
        Check("drive_mismatched_window_refused_before_acquisition", StartDrive(2) != 0 && ResourcesGone());
        yield return CleanupFixture();
        Check("drive_mismatched_native_thread_refused_before_acquisition", StartDrive(3) != 0 && ResourcesGone());
        yield return CleanupFixture();
        Check("drive_mismatched_managed_thread_refused_before_acquisition", StartDrive(4) != 0 && ResourcesGone());
        yield return CleanupFixture();
        int earlyStop = StartDrive(6);
        yield return Until(() => fixture_drive_read(4) == 1 && fixture_drive_read(5) == 1);
        Check("drive_stop_before_client_start_completes_without_resources", earlyStop != 0 && fixture_drive_read(4) == 1 && fixture_drive_read(5) == 1 && ResourcesGone() && Rewired.ReInput.FixtureInputSubscribers == 0);
        Debug.Log("DRIVE_TEST RESULT failures=" + failures);
        Application.Quit(failures == 0 ? 0 : 1);
    }
}
