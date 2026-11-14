using System.Diagnostics;
using System.Runtime.Versioning;

namespace Wsm.Launcher;

public static class RuntimeProbe
{
    [SupportedOSPlatform("windows")]
    public static async Task<int> RunAsync(string runtime, string bridge, string outputDirectory,
        bool mainThreadProbe = false, bool enableLocalCoop = false)
    {
        if (enableLocalCoop && !mainThreadProbe) throw new ArgumentException("Co-op activation requires the visible main-thread probe.");
        runtime = Path.GetFullPath(runtime);
        if (!File.Exists(Path.Combine(runtime, RuntimePreparation.MarkerName)))
            throw new IOException("Use 'prepare' first. Probe only starts a project-owned isolated runtime, never the installed game.");
        var inspection = GameInspection.Inspect(runtime);
        if (inspection.ForeignLoaderFiles.Length != 0)
            throw new IOException("Foreign loaders were found in this runtime; prepare a fresh copy.");
        if (!Path.GetFileName(bridge).Equals("Wsm.Bridge.dll", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("Expected the project's Wsm.Bridge.dll artifact.");
        outputDirectory = Path.GetFullPath(outputDirectory);
        var marker = File.ReadAllLines(Path.Combine(runtime, RuntimePreparation.MarkerName));
        if (marker.Length != 2 || marker[0] != "WSM isolated runtime v1" || !Path.IsPathFullyQualified(marker[1]))
            throw new IOException("Invalid runtime origin marker. Prepare a fresh runtime.");
        var original = Path.TrimEndingDirectorySeparator(Path.GetFullPath(marker[1]));
        RuntimePreparation.RejectReparseAncestors(original);
        RuntimePreparation.RejectReparseAncestors(runtime);
        RuntimePreparation.RejectReparseAncestors(outputDirectory);
        var canonicalOutput = RuntimePreparation.CanonicalPath(outputDirectory);
        if (RuntimePreparation.IsWithin(canonicalOutput, RuntimePreparation.CanonicalPath(original)) || RuntimePreparation.IsWithin(canonicalOutput, RuntimePreparation.CanonicalPath(runtime)))
            throw new IOException("Probe logs must be outside both the original installation and the copied runtime.");
        Directory.CreateDirectory(outputDirectory);
        var run = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N")[..8];
        var log = Path.Combine(outputDirectory, "bridge-" + run + ".log");
        var playerLog = Path.Combine(outputDirectory, "player-" + run + ".log");
        Console.WriteLine(enableLocalCoop ? "Experimental local co-op entry request; not a network multiplayer session." : "Read-only runtime probe; this does not enable multiplayer.");
        Console.WriteLine("Game files are isolated. Unity/Steam may still access the normal user profile; no save redirection is claimed.");
        string[] arguments = mainThreadProbe
            ? ["-screen-fullscreen", "0", "-screen-width", "1280", "-screen-height", "720", "-logFile", playerLog]
            : ["-batchmode", "-nographics", "-logFile", playerLog];
        var child = ChildBootstrap.Start(Path.Combine(runtime, "WarhammerSurvivors.exe"), bridge, log,
            arguments, playerLog, mainThreadProbe, enableLocalCoop);
        Exception? primary = null;
        try
        {
            Console.WriteLine($"Owned child PID: {child.Process.Id}; bridge log: {log}");
            var timer = Stopwatch.StartNew();
            while (timer.Elapsed < TimeSpan.FromSeconds(mainThreadProbe ? 120 : 75))
            {
                var text = ReadShared(log);
                if (InspectionComplete(text, mainThreadProbe, log))
                {
                    var success = text.Contains("status: GameManager=found CharacterController=found;", StringComparison.Ordinal);
                    Console.WriteLine(success ? "LIVE IL2CPP INSPECTION PASSED (no network gameplay adapter installed)." : "Live IL2CPP inspection found an unsupported game API.");
                    if (mainThreadProbe) Console.WriteLine("Unity scene enumeration completed. See the bridge log for actual active instance counts.");
                    if (enableLocalCoop) Console.WriteLine(text.Contains("WSM_COOP_REQUESTED", StringComparison.Ordinal)
                        ? "The existing co-op entry method was invoked. This is NOT proof of functional co-op."
                        : "No co-op entry was invoked; the current scene did not provide a verified activation target.");
                    Console.WriteLine("Unity log: " + playerLog);
                    return success ? 0 : 1;
                }
                if (child.Process.HasExited)
                    throw new IOException($"Game exited with code {child.Process.ExitCode} before inspection. See {playerLog}");
                await Task.Delay(100);
            }
            throw new IOException("Live probe timed out. The owned game process will be stopped. See " + log);
        }
        catch (Exception error) { primary = error; throw; }
        finally { FailureCleanup.Run(child.Dispose, primary); }
    }

    internal static bool InspectionComplete(string text, bool mainThreadProbe, string log)
    {
        if (text.Contains("ERROR:", StringComparison.Ordinal)) throw new IOException("Native bridge failed; see " + log);
        if (text.Contains("inspection thread detached", StringComparison.Ordinal) &&
            (!mainThreadProbe || text.Contains("WSM_MAIN_THREAD_COMPLETE", StringComparison.Ordinal))) return true;
        return false;
    }

    private static string ReadShared(string path)
    {
        if (!File.Exists(path)) return "";
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
