using System.Diagnostics;
using Wsm.App;
using Wsm.Launcher;

internal static class ContextLifecycleRegression
{
    private static async Task Wait(Func<bool> condition, string description)
    {
        var deadline = DateTime.UtcNow.AddSeconds(35);
        while (!condition()) { if (DateTime.UtcNow >= deadline) throw new Exception("Timeout: " + description); await Task.Delay(50); }
    }
    public static async Task Run(string installation, string work)
    {
        var launch = await OwnedGameLaunch.PrepareAndStartAsync(installation, Path.Combine(work, "cache"), Path.Combine(work, "logs"));
        using var observer = Process.GetProcessById(launch.Process.Id);
        var context = new LobbyApplicationContext(null, null);
        var lobby = (LobbyForm)context.MainForm!;
        try
        {
            await Wait(() => { launch.Process.Refresh(); return launch.Process.HasExited || launch.Process.MainWindowHandle != IntPtr.Zero; }, "Unity context fixture window");
            if (launch.Process.HasExited) throw new Exception("Unity context fixture exited early");
            context.TrackRunning(launch);
            if (!lobby.Visible) throw new Exception("Standalone lobby became hidden after game launch.");
            if (lobby.Controls.Find("LobbyScroll", true).Length != 1) throw new Exception("Context lost standalone lobby controls");
            observer.CloseMainWindow();
            var launchButton = (Button)lobby.Controls.Find("LaunchGame", true).Single();
            await Wait(() => observer.HasExited && launchButton.Enabled, "game exit restores live lobby");
            if (lobby.IsDisposed || !lobby.Visible) throw new Exception("Lobby did not survive game exit");
            await using var second = await OwnedGameLaunch.PrepareAndStartAsync(installation, Path.Combine(work, "cache"), Path.Combine(work, "logs"));
            using var secondObserver = Process.GetProcessById(second.Process.Id);
            await Wait(() => { second.Process.Refresh(); return second.Process.HasExited || second.Process.MainWindowHandle != IntPtr.Zero; }, "second Unity window");
            if (second.Process.HasExited) throw new Exception("Second fixture exited early");
            context.TrackRunning(second);
            lobby.Close();
            await Wait(() => lobby.IsDisposed && secondObserver.HasExited, "explicit close stops owned fixture");
            Console.WriteLine("PASS standalone lobby stays accessible with original Unity; game exit restores launch; explicit close awaits cleanup");
        }
        finally
        {
            if (!lobby.IsDisposed) lobby.Close();
            await launch.DisposeAsync();
            context.Dispose();
        }
    }
}
