using Wsm.Launcher;

var root = Path.Combine(Path.GetTempPath(), "wsm-launcher-tests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
var failed = 0;
var count = 0;
var skipped = 0;
try
{
    Run("bridge error wins over completion in every probe mode", () =>
    {
        var text = "inspection thread detached\nWSM_MAIN_THREAD_COMPLETE\nstatus: GameManager=found CharacterController=found;\nERROR: failed";
        foreach (var visible in new[] { false, true })
            Throws<IOException>(() => RuntimeProbe.InspectionComplete(text, visible, "test.log"));
        Check(RuntimeProbe.InspectionComplete(text.Replace("ERROR: failed", ""), true, "test.log"), "valid completion not accepted");
    });
    Run("cleanup retains both exceptions and surfaces cleanup-only failure", () =>
    {
        var primary = new IOException("primary");
        var cleanup = new IOException("cleanup");
        try { FailureCleanup.Run(() => throw cleanup, primary); throw new Exception("expected combined failure"); }
        catch (IOException ex)
        {
            Check(ex.InnerException is AggregateException aggregate && aggregate.InnerExceptions.Contains(primary) && aggregate.InnerExceptions.Contains(cleanup), "both exceptions not preserved");
        }
        try { FailureCleanup.Run(() => throw cleanup, null); throw new Exception("expected cleanup failure"); }
        catch (IOException ex) { Check(ReferenceEquals(ex, cleanup), "cleanup-only exception changed"); }
    });
    Run("owned embedded game launch contract exists", OwnedLaunchRegression.RequireContract);
    Run("owned game stop exposes an awaitable contract", OwnedLaunchRegression.AsyncStopContract);
    Run("hash cancellation interrupts before the next read", OwnedLaunchRegression.HashCancellation);
    if (Environment.GetEnvironmentVariable("WSM_UNITY_FIXTURE_DIRECTORY") is { Length: > 0 } unityFixture)
        Run("original Unity runtime cache preparation, reuse and tamper refusal", () => CacheIntegrationRegression.Run(root, unityFixture));
    Run("filesystem root contains descendants", () =>
    {
        var volume = Path.GetPathRoot(root)!;
        Check(RuntimePreparation.IsWithin(root, volume), "volume root must contain descendant");
    });
    Run("preparation rejects source as destination", () =>
    {
        var game = CreateGame("source-same");
        Throws<ArgumentException>(() => RuntimePreparation.Prepare(game, game));
    });
    Run("preparation preserves original and excludes foreign loaders", () =>
    {
        var game = CreateGame("source-clean");
        File.WriteAllText(Path.Combine(game, "version.dll"), "foreign loader");
        Directory.CreateDirectory(Path.Combine(game, "MelonLoader"));
        File.WriteAllText(Path.Combine(game, "MelonLoader", "private.txt"), "do not copy");
        var target = Path.Combine(root, "clean-runtime");
        RuntimePreparation.Prepare(game, target);
        Check(File.Exists(Path.Combine(target, "WarhammerSurvivors.exe")), "game missing");
        Check(File.Exists(Path.Combine(target, "WarhammerSurvivors_Data", "fixture.txt")), "data missing");
        Check(!File.Exists(Path.Combine(target, "version.dll")), "foreign proxy copied");
        Check(!Directory.Exists(Path.Combine(target, "MelonLoader")), "foreign loader copied");
        File.WriteAllText(Path.Combine(target, "WarhammerSurvivors_Data", "fixture.txt"), "changed");
        Check(File.ReadAllText(Path.Combine(game, "WarhammerSurvivors_Data", "fixture.txt")) == "original", "source changed through a hardlink");
        Check(File.ReadAllText(Path.Combine(game, "version.dll")) == "foreign loader", "existing loader changed");
    });
    Run("preparation refuses an existing destination", () =>
    {
        var game = CreateGame("source-existing");
        var target = Path.Combine(root, "existing");
        Directory.CreateDirectory(target);
        File.WriteAllText(Path.Combine(target, "user.txt"), "keep");
        Throws<IOException>(() => RuntimePreparation.Prepare(game, target));
        Check(File.ReadAllText(Path.Combine(target, "user.txt")) == "keep", "user file modified");
    });
    Run("preparation refuses a descendant destination", () =>
    {
        var game = CreateGame("source-descendant");
        Throws<ArgumentException>(() => RuntimePreparation.Prepare(game, Path.Combine(game, "runtime")));
    });
    Run("preparation rejects missing game files without residue", () =>
    {
        var game = Path.Combine(root, "empty-game");
        Directory.CreateDirectory(game);
        var target = Path.Combine(root, "missing-runtime");
        Throws<FileNotFoundException>(() => RuntimePreparation.Prepare(game, target));
        Check(!Directory.Exists(target), "partial runtime leaked");
    });
    if (args is [var fixtures] && OperatingSystem.IsWindows())
    {
        Run("owned game launch rejects unsafe layout, canceled and unsupported builds before writes", () =>
            OwnedLaunchRegression.Guards(root, CreateInspectableGame("unsupported-owned-launch", fixtures)));
        Run("canceling preparation removes only owned staging and preserves source", () =>
            OwnedLaunchRegression.Cancellation(root, CreateGame("cancel-source")));
        Run("short path aliases cannot bypass preparation containment", () =>
        {
            var game = CreateGame("Long Original Directory Name For Alias");
            var shortGame = ShortPaths.Get(game);
            if (shortGame == null || shortGame.Equals(game, StringComparison.OrdinalIgnoreCase))
                throw new SkipException("filesystem did not provide a distinct 8.3 alias");
            Console.WriteLine("8.3 alias available: " + shortGame);
            Check(RuntimePreparation.CanonicalPath(Path.Combine(shortGame, "new", "runtime")).Equals(Path.Combine(game, "new", "runtime"), StringComparison.OrdinalIgnoreCase), "missing descendant was not canonicalized");
            var target = Path.Combine(game, "new", "runtime");
            Throws<ArgumentException>(() => RuntimePreparation.Prepare(shortGame, target));
            Throws<ArgumentException>(() => RuntimePreparation.Prepare(game, Path.Combine(shortGame, "new", "runtime")));
            Check(!Directory.Exists(Path.Combine(game, "new")), "wrote to original");
            var inspectable = CreateInspectableGame("Long Inspectable Original Name For Alias", fixtures);
            var alias = ShortPaths.Get(inspectable);
            Check(alias != null, "short path lookup failed");
            var runtime = Path.Combine(root, "alias-runtime");
            RuntimePreparation.Prepare(inspectable, runtime);
            File.WriteAllText(Path.Combine(runtime, RuntimePreparation.MarkerName), "WSM isolated runtime v1\n" + alias + "\n");
            var forbidden = Path.Combine(inspectable, "new", "logs");
            try { RuntimeProbe.RunAsync(runtime, Path.Combine(root, "missing/Wsm.Bridge.dll"), forbidden).GetAwaiter().GetResult(); throw new Exception("expected rejection"); }
            catch (IOException ex) { Check(ex.Message == "Probe logs must be outside both the original installation and the copied runtime.", "wrong rejection: " + ex.Message); }
            Check(!Directory.Exists(Path.Combine(inspectable, "new")), "alias probe wrote to original");
            var shortOutput = Path.Combine(alias!, "other-new", "logs");
            try { RuntimeProbe.RunAsync(runtime, Path.Combine(root, "missing/Wsm.Bridge.dll"), shortOutput).GetAwaiter().GetResult(); throw new Exception("expected rejection"); }
            catch (IOException ex) { Check(ex.Message == "Probe logs must be outside both the original installation and the copied runtime.", "wrong alias rejection: " + ex.Message); }
            Check(!Directory.Exists(Path.Combine(inspectable, "other-new")), "alias output wrote to original");
        });
        Run("junction ancestors remain rejected", () =>
        {
            var game = CreateGame("junction-source");
            var junction = Path.Combine(root, "linked-game");
            var start = new System.Diagnostics.ProcessStartInfo("cmd.exe") { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
            foreach (var arg in new[] { "/c", "mklink", "/J", junction, game }) start.ArgumentList.Add(arg);
            using var process = System.Diagnostics.Process.Start(start)!;
            var output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
            process.WaitForExit();
            Check(process.ExitCode == 0, "junction fixture failed: " + output);
            try
            {
                var target = Path.Combine(junction, "new", "runtime");
                try { RuntimePreparation.Prepare(game, target); throw new Exception("expected reparse rejection"); }
                catch (IOException ex) { Check(ex.Message.Contains("Symbolic links and junctions"), "wrong junction rejection: " + ex.Message); }
                Check(!Directory.Exists(Path.Combine(game, "new")), "junction target modified");
            }
            finally { Directory.Delete(junction); }
        });
        Run("COFF objects are not native executable images", () =>
        {
            var file = Path.Combine(root, "not-an-image.dll");
            var bytes = new byte[20];
            System.Buffers.Binary.BinaryPrimitives.WriteUInt16LittleEndian(bytes, 0x8664);
            File.WriteAllBytes(file, bytes);
            Throws<InvalidDataException>(() => GameInspection.RequireNativeX64(file));
        });
        Run("invalid metadata produces a handled CLI diagnostic", () =>
        {
            var game = CreateInspectableGame("invalid-metadata", fixtures);
            File.WriteAllBytes(Path.Combine(game, "WarhammerSurvivors_Data/il2cpp_data/Metadata/global-metadata.dat"), new byte[8]);
            var start = new System.Diagnostics.ProcessStartInfo("dotnet") { RedirectStandardError = true, RedirectStandardOutput = true };
            foreach (var argument in new[] { typeof(GameInspection).Assembly.Location, "inspect", game }) start.ArgumentList.Add(argument);
            using var process = System.Diagnostics.Process.Start(start)!;
            var error = process.StandardError.ReadToEnd();
            Check(process.WaitForExit(5000), "inspection process did not stop");
            Check(process.ExitCode == 1 && error.StartsWith("WSM:"), "invalid metadata was not handled: " + process.ExitCode);
        });
        Run("visible probes reject unprepared game directories", () =>
        {
            foreach (var command in new[] { "ui-probe", "coop-probe" })
            {
                var start = new System.Diagnostics.ProcessStartInfo("dotnet") { RedirectStandardError = true, RedirectStandardOutput = true };
                foreach (var argument in new[] { typeof(GameInspection).Assembly.Location, command, root, "Wsm.Bridge.dll", root }) start.ArgumentList.Add(argument);
                using var process = System.Diagnostics.Process.Start(start)!;
                var error = process.StandardError.ReadToEnd();
                Check(process.WaitForExit(5000), "visible probe process did not stop");
                Check(process.ExitCode == 1 && error.Contains("prepare"), "visible probe did not enforce runtime ownership: " + process.ExitCode);
            }
        });
        Run("probe never creates log directories in original installation", () =>
        {
            var game = CreateInspectableGame("log-source", fixtures);
            var runtime = Path.Combine(root, "log-runtime");
            RuntimePreparation.Prepare(game, runtime);
            var forbidden = Path.Combine(game, "must-not-create");
            try { RuntimeProbe.RunAsync(runtime, Path.Combine(root, "missing/Wsm.Bridge.dll"), forbidden).GetAwaiter().GetResult(); throw new Exception("expected rejection"); }
            catch (IOException ex) { Check(ex.Message == "Probe logs must be outside both the original installation and the copied runtime.", "wrong rejection: " + ex.Message); }
            Check(!Directory.Exists(forbidden), "probe wrote into original installation before rejecting invalid load");
            var safe = Path.Combine(root, "safe-log-sibling");
            try { RuntimeProbe.RunAsync(runtime, Path.Combine(root, "missing/Wsm.Bridge.dll"), safe).GetAwaiter().GetResult(); }
            catch (IOException ex) { Check(!ex.Message.Contains("Probe logs must be outside"), "safe sibling rejected"); }
            Check(Directory.Exists(safe), "safe sibling did not pass isolation check");
        });
        Run("owned native child loads fixture DLL and is stopped on disposal", () =>
        {
            var log = Path.Combine(root, "fixture.log");
            int pid;
            using (var child = ChildBootstrap.Start(Path.Combine(fixtures, "Wsm.BridgeFixture.exe"),
                Path.Combine(fixtures, "Wsm.LoadFixture.dll"), log, []))
            {
                pid = child.Process.Id;
                var deadline = DateTime.UtcNow.AddSeconds(5);
                while (!File.Exists(log) && DateTime.UtcNow < deadline) Thread.Sleep(25);
                Check(File.Exists(log) && File.ReadAllText(log).Contains("WSM_FIXTURE_LOADED"), "fixture DLL did not execute in owned child");
            }
            try
            {
                using var process = System.Diagnostics.Process.GetProcessById(pid);
                Check(process.HasExited, "child leaked after disposal");
            }
            catch (ArgumentException) { }
        });
    }
}
finally { Directory.Delete(root, true); }
Console.WriteLine($"Launcher checks: {count - failed - skipped} passed, {failed} failed, {skipped} skipped, {count} total");
return failed == 0 ? 0 : 1;

string CreateGame(string name)
{
    var path = Path.Combine(root, name);
    Directory.CreateDirectory(Path.Combine(path, "WarhammerSurvivors_Data"));
    foreach (var file in new[] { "WarhammerSurvivors.exe", "UnityPlayer.dll", "GameAssembly.dll" })
        File.WriteAllText(Path.Combine(path, file), "fixture");
    File.WriteAllText(Path.Combine(path, "WarhammerSurvivors_Data", "fixture.txt"), "original");
    return path;
}
string CreateInspectableGame(string name, string fixtures)
{
    var game = CreateGame(name);
    File.Copy(Path.Combine(fixtures, "Wsm.BridgeFixture.exe"), Path.Combine(game, "WarhammerSurvivors.exe"), true);
    foreach (var file in new[] { "UnityPlayer.dll", "GameAssembly.dll" })
        File.Copy(Path.Combine(fixtures, "Wsm.LoadFixture.dll"), Path.Combine(game, file), true);
    var metadata = Path.Combine(game, "WarhammerSurvivors_Data/il2cpp_data/Metadata/global-metadata.dat");
    Directory.CreateDirectory(Path.GetDirectoryName(metadata)!);
    var bytes = new byte[8];
    System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(bytes, 0xfab11baf);
    System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(4), 31);
    File.WriteAllBytes(metadata, bytes);
    return game;
}
void Run(string name, Action test)
{
    count++;
    try { test(); Console.WriteLine("PASS " + name); }
    catch (SkipException ex) { skipped++; Console.WriteLine("SKIP " + name + ": " + ex.Message); }
    catch (Exception ex) { failed++; Console.WriteLine("FAIL " + name + ": " + ex.Message); }
}
static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
static void Throws<T>(Action action) where T : Exception
{
    try { action(); } catch (T) { return; }
    throw new InvalidOperationException("Expected " + typeof(T).Name);
}

static class ShortPaths
{
    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
    private static extern uint GetShortPathNameW(string path, System.Text.StringBuilder result, uint capacity);
    public static string? Get(string path)
    {
        var buffer = new System.Text.StringBuilder(32768);
        var length = GetShortPathNameW(path, buffer, (uint)buffer.Capacity);
        return length == 0 || length >= buffer.Capacity ? null : buffer.ToString();
    }
}

sealed class SkipException(string message) : Exception(message);
