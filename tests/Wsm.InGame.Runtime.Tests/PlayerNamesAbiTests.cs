using System.Text;
using Wsm.InGame.Runtime;

internal static class PlayerNamesAbiTests
{
    public static async Task Run()
    {
        using var entered = new ManualResetEventSlim();
        using var resume = new ManualResetEventSlim();
        var controller = NativeExports.ControllerForTests;
        controller.BeforeOperationStart = () => { entered.Set(); if (!resume.Wait(TimeSpan.FromSeconds(5))) throw new TimeoutException(); };
        try
        {
            if (Set([0xff]) != -1 || Set(Encoding.UTF8.GetBytes("Erste😀")) != 0) throw new Exception("UTF-8 validation");
            if (Host() != 0 || !entered.Wait(TimeSpan.FromSeconds(5))) throw new Exception("host not queued");
            if (Set(Encoding.UTF8.GetBytes("Nächste")) != 0) throw new Exception("set next name");
            resume.Set();
            await WaitFor(() => controller.Status.State == IngameState.Hosting);
            if (Read(true) != "Erste😀" || Read(false) != "Erste😀") throw new Exception("queued name was relabeled");
            await using var guest = new IngameSessionController();
            if (await guest.JoinAsync("127.0.0.1", controller.Status.BoundPort, displayName: "Gast") != TransitionResult.Started) throw new Exception("guest admission");
            await WaitFor(() => Read(false) == "Erste😀\nGast");
            if (!SmallBufferUnchanged()) throw new Exception("insufficient buffer modified");
            await guest.LeaveAsync();
            await WaitFor(() => Read(false) == "Erste😀");
        }
        finally { controller.BeforeOperationStart = null; resume.Set(); Leave(); await WaitFor(() => controller.Status.State == IngameState.Disconnected); }
        if (Read(false) != "" || Read(true) != "") throw new Exception("stale native name");
    }
    private static async Task WaitFor(Func<bool> predicate)
    {
        var until = DateTime.UtcNow.AddSeconds(10);
        while (!predicate()) { if (DateTime.UtcNow >= until) throw new Exception("roster did not converge"); await Task.Delay(20); }
    }
    private static unsafe int Set(byte[] value) { fixed (byte* p = value) { delegate* unmanaged[Cdecl]<byte*, int, int> f = &NativeExports.SetPlayerName; return f(p, value.Length); } }
    private static unsafe int Host() { delegate* unmanaged[Cdecl]<int, int, int> f = &NativeExports.Host; return f(0, 0); }
    private static unsafe int Leave() { delegate* unmanaged[Cdecl]<int> f = &NativeExports.Leave; return f(); }
    private static unsafe string Read(bool local)
    {
        delegate* unmanaged[Cdecl]<byte*, int, int> f = local ? &NativeExports.LocalPlayerName : &NativeExports.PlayerNames;
        int n = f(null, 0); if (n < 0 || n > 4096) throw new Exception("invalid size");
        var bytes = new byte[n + 1]; fixed (byte* p = bytes) { if (f(p, bytes.Length) != n) throw new Exception("copy failed"); }
        return Encoding.UTF8.GetString(bytes, 0, n);
    }
    private static unsafe bool SmallBufferUnchanged()
    {
        byte value = 0x7f; delegate* unmanaged[Cdecl]<byte*, int, int> f = &NativeExports.PlayerNames;
        return f(&value, 1) == -5 && value == 0x7f;
    }
}
