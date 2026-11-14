using System.Diagnostics;
using System.Reflection;
using Wsm.Launcher;

internal static class OwnedStopRegression
{
    public static async Task Run(string fixture)
    {
        var process = Process.Start(new ProcessStartInfo(fixture) { UseShellExecute = false, ArgumentList = { "--ignore-close" } })
            ?? throw new Exception("Owned stop fixture did not start.");
        using var observer = Process.GetProcessById(process.Id);
        // Exercise lifetime handling of a test-owned child, not game preparation/compatibility.
        var constructor = typeof(OwnedGameLaunch).GetConstructor(BindingFlags.NonPublic | BindingFlags.Instance, null,
            [typeof(Process), typeof(string), typeof(string)], null) ?? throw new Exception("Owned launch constructor not found.");
        var launch = (OwnedGameLaunch)constructor.Invoke([process, Path.GetDirectoryName(fixture)!, "fixture-only"]);
        using var timer = new System.Windows.Forms.Timer { Interval = 25 };
        int ticks = 0; timer.Tick += (_, _) => ticks++;
        try
        {
            var deadline = DateTime.UtcNow.AddSeconds(12);
            while (process.MainWindowHandle == IntPtr.Zero)
            {
                if (process.HasExited || DateTime.UtcNow > deadline) throw new Exception("Owned stop fixture window missing.");
                process.Refresh(); await Task.Delay(30);
            }
            timer.Start();
            var watch = Stopwatch.StartNew();
            var stop = launch.StopAsync();
            if (stop.IsCompleted) throw new Exception("Ignored-close stop completed synchronously instead of awaiting grace.");
            if (!ReferenceEquals(stop, launch.StopAsync())) throw new Exception("Concurrent stops did not share their operation.");
            await stop.WaitAsync(TimeSpan.FromSeconds(12));
            if (!observer.HasExited) throw new Exception("Owned child survived stop.");
            if (ticks < 10) throw new Exception("UI message processing was blocked during graceful stop.");
            if (watch.Elapsed < TimeSpan.FromSeconds(2.5)) throw new Exception("Ignored-close fixture did not exercise the grace period.");
            await launch.StopAsync(); await launch.DisposeAsync();
            Console.WriteLine("PASS asynchronous owned-child stop: ignored close, UI ticks, shared stop, kill escalation and idempotent cleanup");
        }
        finally { timer.Stop(); await launch.DisposeAsync(); }
    }
}
