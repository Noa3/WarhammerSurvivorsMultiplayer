using System.Runtime.Versioning;

namespace Wsm.Launcher;

public static class InGameLaunch
{
    [SupportedOSPlatform("windows")]
    public static async Task<int> RunAsync(string game, string cache, string bridge, string logs, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        game = Path.GetFullPath(game);
        cache = Path.GetFullPath(cache);
        bridge = Path.GetFullPath(bridge);
        logs = Path.GetFullPath(logs);
        if (!Path.GetFileName(bridge).Equals("Wsm.Bridge.dll", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("Expected the project's Wsm.Bridge.dll artifact.");
        RuntimePreparation.RejectReparseAncestors(Path.GetDirectoryName(bridge)!);
        RuntimePreparation.RejectReparseAncestors(logs);
        var canonicalLogs = RuntimePreparation.CanonicalPath(logs);
        if (RuntimePreparation.IsWithin(canonicalLogs, RuntimePreparation.CanonicalPath(game)) ||
            RuntimePreparation.IsWithin(canonicalLogs, RuntimePreparation.CanonicalPath(cache)))
            throw new ArgumentException("Game logs must be outside the original installation and runtime cache.");
        var nativeRuntime = Path.Combine(Path.GetDirectoryName(bridge)!, "Wsm.InGame.Runtime.dll");
        GameInspection.RequireNativeX64(bridge);
        GameInspection.RequireNativeX64(nativeRuntime);
        if ((File.GetAttributes(bridge) & FileAttributes.ReparsePoint) != 0 ||
            (File.GetAttributes(nativeRuntime) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("Bridge and adjacent runtime must not be symbolic links or junctions.");

        Console.WriteLine("The installation copy is isolated, but the game may use your normal profile, saves and Steam Cloud; these are not isolated.");
        var runtime = await OwnedGameLaunch.PrepareRuntimeAsync(game, cache, cancellationToken).ConfigureAwait(false);
        cancellationToken.ThrowIfCancellationRequested();
        RuntimePreparation.RejectReparseAncestors(logs);
        Directory.CreateDirectory(logs);
        var run = Guid.NewGuid().ToString("N");
        var playerLog = Path.Combine(logs, "player-" + run + ".log");
        var bridgeLog = Path.Combine(logs, "bridge-" + run + ".log");
        var child = ChildBootstrap.Start(Path.Combine(runtime, "WarhammerSurvivors.exe"), bridge, bridgeLog,
            ["-logFile", playerLog], playerLog, enableInGameUi: true);
        Exception? primary = null;
        try
        {
            Console.WriteLine($"Owned game PID: {child.Process.Id}; bridge log: {bridgeLog}; player log: {playerLog}");
            await child.Process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
            return child.Process.ExitCode;
        }
        catch (Exception error) { primary = error; throw; }
        finally { FailureCleanup.Run(child.Dispose, primary); }
    }
}
