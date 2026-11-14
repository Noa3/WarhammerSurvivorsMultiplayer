using Wsm.Launcher;

internal static class OwnedLaunchRegression
{
    public static void RequireContract()
    {
        var type = typeof(GameInspection).Assembly.GetType("Wsm.Launcher.OwnedGameLaunch");
        if (type?.GetMethod("PrepareAndStartAsync") is null)
            throw new Exception("An owned, prepared-runtime game-launch path for the embedded lobby is missing.");
    }
    public static void AsyncStopContract()
    {
        if (typeof(OwnedGameLaunch).GetMethod("StopAsync")?.ReturnType != typeof(Task))
            throw new Exception("Owned game shutdown lacks an awaitable, bounded stop operation.");
    }
    public static void HashCancellation()
    {
        using var canceled = new CancellationTokenSource();
        using var stream = new CancelingStream(canceled);
        Throws<OperationCanceledException>(() => GameInspection.HashStream(stream, canceled.Token));
        if (stream.Reads != 1) throw new Exception("Hashing continued reading after cancellation.");
    }
    private sealed class CancelingStream(CancellationTokenSource cancellation) : MemoryStream(new byte[512 * 1024])
    {
        public int Reads { get; private set; }
        public override int Read(Span<byte> buffer)
        {
            Reads++; var read = base.Read(buffer); cancellation.Cancel(); return read;
        }
    }
    public static void Guards(string root, string unsupportedGame)
    {
        var cache = Path.Combine(root, "owned-cache");
        var logs = Path.Combine(root, "owned-logs");
        using var canceled = new CancellationTokenSource(); canceled.Cancel();
        Throws<OperationCanceledException>(() => OwnedGameLaunch.PrepareAndStartAsync(unsupportedGame, cache, logs, canceled.Token).GetAwaiter().GetResult());
        Throws<ArgumentException>(() => OwnedGameLaunch.PrepareAndStartAsync(unsupportedGame, Path.Combine(unsupportedGame, "cache"), logs).GetAwaiter().GetResult());
        Throws<ArgumentException>(() => OwnedGameLaunch.PrepareAndStartAsync(unsupportedGame, cache, Path.Combine(unsupportedGame, "logs")).GetAwaiter().GetResult());
        Throws<InvalidDataException>(() => OwnedGameLaunch.PrepareAndStartAsync(unsupportedGame, cache, logs).GetAwaiter().GetResult());
        if (Directory.Exists(cache) || Directory.Exists(logs)) throw new Exception("Rejected launch created cache/log output before validating its source.");
    }
    public static void Cancellation(string root, string source)
    {
        var target = Path.Combine(root, "canceled-copy");
        var data = Path.Combine(source, "WarhammerSurvivors_Data");
        for (int i = 0; i < 256; i++)
        {
            using var stream = new FileStream(Path.Combine(data, "cancel-" + i + ".bin"), FileMode.CreateNew);
            stream.SetLength(256 * 1024);
        }
        using var stop = new CancellationTokenSource();
        var copying = Task.Run(() => RuntimePreparation.Prepare(source, target, stop.Token));
        var timeout = DateTime.UtcNow.AddSeconds(3);
        while (!Directory.EnumerateDirectories(root, ".wsm-staging-*").Any() && !copying.IsCompleted && DateTime.UtcNow < timeout) Thread.Sleep(1);
        if (copying.IsCompleted) { copying.GetAwaiter().GetResult(); throw new Exception("Cancellation fixture completed before reaching the staged-copy boundary."); }
        stop.Cancel();
        Throws<OperationCanceledException>(() => copying.GetAwaiter().GetResult());
        if (Directory.Exists(target) || Directory.EnumerateDirectories(root, ".wsm-staging-*").Any())
            throw new Exception("Canceled copy leaked a committed runtime or temporary staging files.");
        if (Directory.GetFiles(data, "cancel-*.bin").Length != 256) throw new Exception("Cancellation changed source files.");
    }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception("Expected " + typeof(T).Name);
    }
}
