using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;

public sealed class InputPumpFixture : MonoBehaviour
{
    [DllImport("kernel32")] static extern uint GetCurrentThreadId();
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern int fixture_pump_start(int managed, uint os, int mode);
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_pump_stop();
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_pump_drop_worker();
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern void fixture_pump_block_cleanup(int blocked);
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern ulong fixture_pump_read(int field);
    [DllImport("fixture", CallingConvention = CallingConvention.Cdecl)] static extern IntPtr fixture_pump_reason();
    int failures;
    int managed;
    uint os;
    void Check(string name, bool ok)
    {
        Debug.Log("PUMP_TEST " + name + "=" + (ok ? "PASS" : "FAIL"));
        if (!ok) ++failures;
    }
    int StartPump(int mode = 0, int managedDelta = 0, uint osDelta = 0)
    {
        int result = fixture_pump_start(managed + managedDelta, os + osDelta, mode);
        if (result != 0) Debug.Log("PUMP_REASON " + Marshal.PtrToStringAnsi(fixture_pump_reason()));
        return result;
    }
    IEnumerator WaitUntilBounded(Func<bool> predicate)
    {
        float end = Time.realtimeSinceStartup + 6;
        while (!predicate() && Time.realtimeSinceStartup < end) yield return null;
    }
    IEnumerator FinishOwner()
    {
        fixture_pump_stop();
        yield return WaitUntilBounded(() => fixture_pump_read(3) == 1 && fixture_pump_read(10) == 0);
    }
    IEnumerator Start()
    {
        managed = Thread.CurrentThread.ManagedThreadId;
        os = GetCurrentThreadId();
        bool contract;
        try { contract = StartPump(0, 0, 1) != 0; }
        catch (EntryPointNotFoundException) { contract = false; }
        Check("pump_contract", contract);
        if (!contract) { Finish(); yield break; }
        Check("pump_wrong_window_thread_refused", Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("pump_wrong_managed_thread_refused", StartPump(0, 1) != 0 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("pump_subscribe", StartPump() == 0 && Rewired.ReInput.FixtureInputSubscribers == 1);
        var first = Rewired.ReInput.FixtureCaptureInput();
        Rewired.ReInput.FixtureFireInput();
        Rewired.ReInput.FixtureFireInput();
        Check("pump_actual_event_delivery", fixture_pump_read(0) == 2 && fixture_pump_read(9) == 1);
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Rewired.ReInput.FixtureFireInput();
        Check("pump_callback_survives_gc", fixture_pump_read(0) == 3);
        yield return WaitUntilBounded(() => fixture_pump_read(1) > 0);
        Check("pump_independent_recovery_runs", fixture_pump_read(1) > 0 && fixture_pump_read(10) == 1);
        yield return FinishOwner();
        Check("pump_stop_without_more_input_events", fixture_pump_read(2) == 1 && fixture_pump_read(3) == 1 && fixture_pump_read(10) == 0 && Rewired.ReInput.FixtureInputSubscribers == 0);
        first?.Invoke();
        Check("pump_late_callback_tombstone", fixture_pump_read(0) == 3);

        Check("pump_start_before_wrapper_drop", StartPump() == 0);
        var retained = Rewired.ReInput.FixtureCaptureInput();
        fixture_pump_block_cleanup(1);
        fixture_pump_drop_worker();
        yield return WaitUntilBounded(() => fixture_pump_read(1) >= 2);
        Check("pump_owner_survives_producer_destruction", fixture_pump_read(4) == 1 && fixture_pump_read(8) == 0 && fixture_pump_read(2) == 0 && Rewired.ReInput.FixtureInputSubscribers == 1);
        fixture_pump_block_cleanup(0);
        yield return WaitUntilBounded(() => fixture_pump_read(2) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("pump_cleanup_retries_after_wrapper_gone", fixture_pump_read(2) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("pump_new_owner_after_cleanup", StartPump() == 0);
        retained?.Invoke();
        Check("pump_old_action_never_routes_new_owner", fixture_pump_read(0) == 0);
        Rewired.ReInput.FixtureFireInput();
        Check("pump_new_action_still_effective", fixture_pump_read(0) == 1);
        yield return FinishOwner();

        int failedStart = StartPump(1);
        yield return WaitUntilBounded(() => fixture_pump_read(3) == 1);
        Check("pump_partial_client_start_retains_cleanup", failedStart != 0 && fixture_pump_read(2) == 1 && fixture_pump_read(3) == 1);
        Rewired.ReInput.FixtureFailAddBefore = true;
        int addBefore = StartPump();
        Rewired.ReInput.FixtureFailAddBefore = false;
        yield return WaitUntilBounded(() => fixture_pump_read(3) == 1);
        Check("pump_add_before_effect_failure_recovers", addBefore != 0 && fixture_pump_read(2) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Rewired.ReInput.FixtureFailAddAfter = true;
        int addAfter = StartPump();
        Rewired.ReInput.FixtureFailAddAfter = false;
        yield return WaitUntilBounded(() => fixture_pump_read(3) == 1);
        Check("pump_add_after_effect_failure_recovers", addAfter != 0 && fixture_pump_read(2) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);

        Check("pump_start_before_remove_failure", StartPump() == 0);
        Rewired.ReInput.FixtureFailRemoveBefore = true;
        fixture_pump_stop();
        yield return WaitUntilBounded(() => fixture_pump_read(1) >= 3);
        ulong stoppedInputs = fixture_pump_read(0);
        Rewired.ReInput.FixtureFireInput();
        Check("pump_remove_failure_retains_subscription", fixture_pump_read(7) == 1 && fixture_pump_read(3) == 0 && Rewired.ReInput.FixtureInputSubscribers == 1 && fixture_pump_read(0) == stoppedInputs);
        Rewired.ReInput.FixtureFailRemoveBefore = false;
        yield return FinishOwner();
        Check("pump_remove_retry_completes", fixture_pump_read(3) == 1 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("pump_start_before_ambiguous_remove", StartPump() == 0);
        Rewired.ReInput.FixtureFailRemoveAfter = true;
        fixture_pump_stop();
        yield return WaitUntilBounded(() => fixture_pump_read(1) >= 3);
        Check("pump_remove_after_effect_is_still_pending", Rewired.ReInput.FixtureInputSubscribers == 0 && fixture_pump_read(7) == 1 && fixture_pump_read(3) == 0);
        Rewired.ReInput.FixtureFailRemoveAfter = false;
        yield return FinishOwner();
        Check("pump_ambiguous_remove_retry_acknowledged", fixture_pump_read(3) == 1);

        Check("pump_start_before_foreign_callback", StartPump() == 0);
        var foreign = Rewired.ReInput.FixtureCaptureInput();
        var task = Task.Run(() => foreign?.Invoke());
        yield return WaitUntilBounded(() => task.IsCompleted);
        Check("pump_actual_foreign_callback_refused", task.IsCompleted && !task.IsFaulted && fixture_pump_read(5) > 0 && fixture_pump_read(0) == 0);
        yield return FinishOwner();
        Check("pump_foreign_callback_cleanup", fixture_pump_read(3) == 1);
        Check("pump_start_before_reentry", StartPump(2) == 0);
        Rewired.ReInput.FixtureFireInput();
        Check("pump_reentrant_callback_stops_without_recursion", fixture_pump_read(6) > 0 && fixture_pump_read(0) == 1);
        yield return FinishOwner();
        Check("pump_reentrant_cleanup", fixture_pump_read(3) == 1);
        Check("pump_start_before_bad_context", StartPump() == 0);
        var context = SynchronizationContext.Current;
        try { SynchronizationContext.SetSynchronizationContext(new SynchronizationContext()); Rewired.ReInput.FixtureFireInput(); }
        finally { SynchronizationContext.SetSynchronizationContext(context); }
        yield return FinishOwner();
        Check("pump_substituted_context_refused", fixture_pump_read(0) == 0 && fixture_pump_read(3) == 1);
        Rewired.ReInput.FixtureResetInputFaults();
        Check("pump_final_no_subscription", Rewired.ReInput.FixtureInputSubscribers == 0 && fixture_pump_read(8) == 0);
        Check("pump_pin_failure_terminal", StartPump(3) != 0 && fixture_pump_read(3) == 1 && fixture_pump_read(10) == 0 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("pump_retry_after_pin_failure", StartPump() == 0); yield return FinishOwner();
        Check("pump_gate_failure_terminal", StartPump(4) != 0 && fixture_pump_read(3) == 1 && fixture_pump_read(10) == 0 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("pump_retry_after_gate_failure", StartPump() == 0); yield return FinishOwner();
        Check("pump_worker_failure_terminal", StartPump(5) != 0 && fixture_pump_read(3) == 1 && fixture_pump_read(10) == 0 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("pump_retry_after_worker_failure", StartPump() == 0); yield return FinishOwner();
        Check("pump_slot_failure_terminal", StartPump(6) != 0 && fixture_pump_read(3) == 1 && fixture_pump_read(10) == 0 && Rewired.ReInput.FixtureInputSubscribers == 0);
        Check("pump_retry_after_slot_failure", StartPump() == 0); yield return FinishOwner();
        Finish();
    }
    void Finish()
    {
        Debug.Log("PUMP_TEST RESULT failures=" + failures);
        Application.Quit(failures == 0 ? 0 : 1);
    }
}
