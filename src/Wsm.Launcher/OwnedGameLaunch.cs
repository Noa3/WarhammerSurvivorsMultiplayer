using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Wsm.Launcher;

/// <summary>Owns one explicitly launched copied game. Copy isolation does not isolate saves or Steam Cloud.</summary>
public sealed class OwnedGameLaunch : IDisposable, IAsyncDisposable
{
    private const string CacheMarker = ".wsm-cache.json";
    private sealed record CacheIdentity(int Schema, string Source, string ContentSignature);
    private static readonly SemaphoreSlim PreparationGate = new(1, 1);
    private int disposed;
    private readonly object stopGate = new();
    private Task? stopping;
    public Process Process { get; }
    public string RuntimeDirectory { get; }
    public string PlayerLog { get; }
    private OwnedGameLaunch(Process process, string runtime, string log)
    { Process = process; RuntimeDirectory = runtime; PlayerLog = log; }

    public static async Task<OwnedGameLaunch> PrepareAndStartAsync(string installation, string runtimeCacheRoot,
        string logDirectory, CancellationToken cancellationToken = default)
    {
        if (!OperatingSystem.IsWindows() || !Environment.Is64BitProcess)
            throw new PlatformNotSupportedException("The embedded game launcher requires Windows x64.");
        cancellationToken.ThrowIfCancellationRequested();
        var paths = ValidateLayout(installation, runtimeCacheRoot, logDirectory);
        var runtime = await PrepareRuntimeAsync(paths.Source, paths.Cache, cancellationToken).ConfigureAwait(false);
        RuntimePreparation.RejectReparseAncestors(paths.Logs);
        if (RuntimePreparation.IsWithin(RuntimePreparation.CanonicalPath(paths.Logs), RuntimePreparation.CanonicalPath(runtime)))
            throw new ArgumentException("Game logs must be outside the prepared runtime.");
        cancellationToken.ThrowIfCancellationRequested();
        Directory.CreateDirectory(paths.Logs);
        var log = Path.Combine(paths.Logs, "player-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N") + ".log");
        var info = new ProcessStartInfo(Path.Combine(runtime, "WarhammerSurvivors.exe"))
        {
            WorkingDirectory = runtime, UseShellExecute = false
        };
        foreach (var value in new[] { "-screen-fullscreen", "0", "-screen-width", "1280", "-screen-height", "720", "-logFile", log })
            info.ArgumentList.Add(value);
        info.Environment["SteamAppId"] = "3669620";
        info.Environment["SteamGameId"] = "3669620";
        foreach (var variable in new[] { "WSM_BRIDGE_LOG", "WSM_GAME_LOG", "WSM_MAIN_THREAD_PROBE", "WSM_ENABLE_LOCAL_COOP" })
            info.Environment.Remove(variable);
        var process = Process.Start(info) ?? throw new IOException("The prepared game did not start.");
        var owned = new OwnedGameLaunch(process, runtime, log);
        try { cancellationToken.ThrowIfCancellationRequested(); return owned; }
        catch { owned.Dispose(); throw; }
    }

    /// <summary>Prepares and validates binaries only. Never starts a game or reads save data.</summary>
    public static async Task<string> PrepareRuntimeAsync(string installation, string runtimeCacheRoot,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var source = Path.TrimEndingDirectorySeparator(Path.GetFullPath(installation));
        var cache = Path.TrimEndingDirectorySeparator(Path.GetFullPath(runtimeCacheRoot));
        RejectOverlap(source, cache);
        await PreparationGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var inspection = await Task.Run(() => GameInspection.Inspect(source, cancellationToken), cancellationToken).ConfigureAwait(false);
            if (inspection.MetadataVersion != 31 ||
                !(inspection.ProductVersion == "6000.0.74f1" || inspection.ProductVersion.StartsWith("6000.0.74f1 (", StringComparison.Ordinal)))
                throw new InvalidDataException("Unsupported Unity/IL2CPP build for the embedded launcher: " + inspection.ProductVersion + ", metadata " + inspection.MetadataVersion + ".");
            var signature = await Task.Run(() => RuntimeContentSignature.Capture(source, cancellationToken), cancellationToken).ConfigureAwait(false);
            var identity = inspection.Directory + "\n" + signature;
            var key = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(identity)))[..24];
            var runtime = Path.Combine(cache, key);
            RuntimePreparation.RejectReparseAncestors(runtime);
            cancellationToken.ThrowIfCancellationRequested();
            if (!Directory.Exists(runtime))
            {
                var staging = Path.Combine(cache, ".wsm-session-" + Guid.NewGuid().ToString("N"));
                bool ownsStaging = false;
                Exception? primary = null;
                try
                {
                    await Task.Run(() => RuntimePreparation.Prepare(source, staging, cancellationToken), cancellationToken).ConfigureAwait(false);
                    ownsStaging = true;
                    var copied = await Task.Run(() => GameInspection.Inspect(staging, cancellationToken), cancellationToken).ConfigureAwait(false);
                    ValidateCopiedRuntime(source, staging, inspection, copied);
                    if (await Task.Run(() => RuntimeContentSignature.Capture(staging, cancellationToken), cancellationToken).ConfigureAwait(false) != signature)
                        throw new IOException("Included game files changed during copying; no runtime was committed.");
                    File.WriteAllText(Path.Combine(staging, CacheMarker), JsonSerializer.Serialize(new CacheIdentity(2, inspection.Directory, signature)));
                    var sourceNow = await Task.Run(() => GameInspection.Inspect(source, cancellationToken), cancellationToken).ConfigureAwait(false);
                    if (!SameHashes(sourceNow.Sha256, inspection.Sha256) || await Task.Run(() => RuntimeContentSignature.Capture(source, cancellationToken), cancellationToken).ConfigureAwait(false) != signature) throw new IOException("The installation changed during preparation; prepare a fresh runtime.");
                    cancellationToken.ThrowIfCancellationRequested();
                    Directory.Move(staging, runtime);
                    ownsStaging = false;
                }
                catch (Exception ex) { primary = ex; throw; }
                finally { FailureCleanup.Run(() => { if (ownsStaging && Directory.Exists(staging)) Directory.Delete(staging, true); }, primary); }
            }
            else
            {
                var marker = Path.Combine(runtime, CacheMarker);
                if (!File.Exists(marker) || (File.GetAttributes(marker) & FileAttributes.ReparsePoint) != 0 || new FileInfo(marker).Length > 16_384)
                    throw new IOException("An existing runtime cache is not trusted. Choose a new cache directory; no files were overwritten.");
                var prior = JsonSerializer.Deserialize<CacheIdentity>(File.ReadAllText(marker))
                    ?? throw new InvalidDataException("Invalid runtime cache record.");
                if (prior.Schema != 2 || !string.Equals(prior.Source, inspection.Directory, StringComparison.OrdinalIgnoreCase) || prior.ContentSignature != signature)
                    throw new InvalidDataException("Runtime cache does not match this game installation.");
                ValidateCopiedRuntime(source, runtime, inspection,
                    await Task.Run(() => GameInspection.Inspect(runtime, cancellationToken), cancellationToken).ConfigureAwait(false));
                if (await Task.Run(() => RuntimeContentSignature.Capture(runtime, cancellationToken), cancellationToken).ConfigureAwait(false) != signature)
                    throw new InvalidDataException("A cached runtime file was added, changed or removed; choose a fresh cache directory.");
            }
            // A source update during copying must not create a stale or mixed launch candidate.
            var latest = await Task.Run(() => GameInspection.Inspect(source, cancellationToken), cancellationToken).ConfigureAwait(false);
            if (!SameHashes(latest.Sha256, inspection.Sha256) || await Task.Run(() => RuntimeContentSignature.Capture(source, cancellationToken), cancellationToken).ConfigureAwait(false) != signature) throw new IOException("The installation changed during preparation; prepare a fresh runtime.");
            cancellationToken.ThrowIfCancellationRequested();
            return runtime;
        }
        finally { PreparationGate.Release(); }
    }

    private static void ValidateCopiedRuntime(string source, string runtime, GameInspection expected, GameInspection actual)
    {
        var marker = Path.Combine(runtime, RuntimePreparation.MarkerName);
        if (!File.Exists(marker) || (File.GetAttributes(marker) & FileAttributes.ReparsePoint) != 0 || new FileInfo(marker).Length > 32_768)
            throw new IOException("Invalid copied-runtime ownership marker.");
        var origin = File.ReadAllLines(marker);
        if (origin.Length != 2 || origin[0] != "WSM isolated runtime v1" ||
            !string.Equals(RuntimePreparation.CanonicalPath(origin[1]), RuntimePreparation.CanonicalPath(source), StringComparison.OrdinalIgnoreCase) ||
            actual.ForeignLoaderFiles.Length != 0 || !SameHashes(expected.Sha256, actual.Sha256))
            throw new InvalidDataException("The copied runtime was changed or does not belong to this installation.");
    }
    private static bool SameHashes(IReadOnlyDictionary<string, string>? a, IReadOnlyDictionary<string, string>? b) =>
        a is not null && b is not null && a.Count == b.Count && a.All(pair => b.TryGetValue(pair.Key, out var value) && pair.Value == value);
    private static (string Source, string Cache, string Logs) ValidateLayout(string source, string cache, string logs)
    {
        source = Path.TrimEndingDirectorySeparator(Path.GetFullPath(source));
        cache = Path.TrimEndingDirectorySeparator(Path.GetFullPath(cache));
        logs = Path.TrimEndingDirectorySeparator(Path.GetFullPath(logs));
        RejectOverlap(source, cache);
        RuntimePreparation.RejectReparseAncestors(logs);
        if (RuntimePreparation.IsWithin(RuntimePreparation.CanonicalPath(logs), RuntimePreparation.CanonicalPath(source)))
            throw new ArgumentException("Game logs must be outside the original installation.");
        return (source, cache, logs);
    }
    private static void RejectOverlap(string source, string cache)
    {
        RuntimePreparation.RejectReparseAncestors(source);
        RuntimePreparation.RejectReparseAncestors(cache);
        source = RuntimePreparation.CanonicalPath(source); cache = RuntimePreparation.CanonicalPath(cache);
        if (RuntimePreparation.IsWithin(source, cache) || RuntimePreparation.IsWithin(cache, source))
            throw new ArgumentException("The runtime cache must be separate from the original installation.");
    }
    /// <summary>UI callers must await this operation. A failed stop retains ownership for retry.</summary>
    public Task StopAsync()
    {
        lock (stopGate)
        {
            if (Volatile.Read(ref disposed) != 0) return Task.CompletedTask;
            if (stopping is null || stopping.IsFaulted || stopping.IsCanceled) stopping = StopCoreAsync();
            return stopping;
        }
    }
    private async Task StopCoreAsync()
    {
        if (!Process.HasExited)
        {
            if (Process.CloseMainWindow())
            {
                try { await Process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(3)).ConfigureAwait(false); }
                catch (TimeoutException) { /* Escalate only this owned child after the grace period. */ }
            }
            if (!Process.HasExited) Process.Kill(entireProcessTree: false);
            try { await Process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false); }
            catch (TimeoutException ex) { throw new IOException("The owned game process did not stop; ownership is retained for retry.", ex); }
        }
        Process.Dispose();
        Volatile.Write(ref disposed, 1);
    }
    public ValueTask DisposeAsync() => new(StopAsync());
    public void Dispose() => StopAsync().GetAwaiter().GetResult();
}
