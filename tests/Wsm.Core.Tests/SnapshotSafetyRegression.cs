using System.Net;
using Wsm.Core;

internal static class SnapshotSafetyRegression
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }

    public static async Task Capacity()
    {
        foreach (char suffix in new[] { 'x', '\\', 'é' }) await CapacityFor(suffix);
    }

    private static async Task CapacityFor(char suffix)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        await using var client = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token);
        int accepted = 0;
        for (int i = 0; i < 2048; i++)
        {
            string id = i.ToString("D4") + new string(suffix, 124);
            try { host.Encounter.RegisterEnemy(id, int.MaxValue); accepted++; }
            catch (InvalidOperationException) { break; }
        }
        Check(host.Encounter.Snapshot().Length == accepted, "failed registration mutated encounter");
        await host.PublishSnapshotAsync(new(long.MaxValue, []), timeout.Token);
        var snapshot = await client.Snapshots.ReadAsync(timeout.Token);
        Check(snapshot.Enemies.Length == accepted, "admitted boundary must publish whole state");
        Check(accepted > 0 && accepted < 2048, "escaped enemy frame budget must reject before poisoning state");
        int encoded = Peer.Encode(new Message("snapshot", Snapshot: new WorldSnapshot(long.MaxValue, []) { Enemies = snapshot.Enemies })).Length;
        Check(encoded <= Peer.MaxFrame, "admitted boundary exceeds frame");
        var extra = new EnemyHealth(accepted.ToString("D4") + new string(suffix, 124), int.MaxValue, int.MaxValue);
        try { Peer.Encode(new Message("snapshot", Snapshot: new WorldSnapshot(long.MaxValue, []) { Enemies = [.. snapshot.Enemies, extra] })); throw new Exception("rejected enemy would have fit"); }
        catch (InvalidDataException) { }
    }

    public static async Task TickOrder()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        await using var client = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token);
        host.Encounter.RegisterEnemy("orc", 1);
        await host.PublishSnapshotAsync(new(10, []), timeout.Token);
        await client.Snapshots.ReadAsync(timeout.Token);
        host.Encounter.ApplyValidatedHit(Guid.NewGuid(), "orc", 1);
        var releaseOlder = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var older = Task.Run(async () => { await releaseOlder.Task; await host.PublishSnapshotAsync(new(10, []), timeout.Token); });
        await Task.Run(() => host.PublishSnapshotAsync(new(11, []), timeout.Token));
        var dead = await client.Snapshots.ReadAsync(timeout.Token);
        Check(dead.Tick == 11 && dead.Enemies.Single().Hp == 0, "newer death not delivered");
        releaseOlder.SetResult();
        try { await older; throw new Exception("stale tick accepted after newer death"); }
        catch (ArgumentException) { }
        using var cancelled = new CancellationTokenSource(); cancelled.Cancel();
        try { await host.PublishSnapshotAsync(new(12, []), cancelled.Token); throw new Exception("cancelled publish accepted"); }
        catch (OperationCanceledException) { }
        await host.PublishSnapshotAsync(new(12, []), timeout.Token);
        Check((await client.Snapshots.ReadAsync(timeout.Token)).Tick == 12, "cancel advanced tick");
        var start = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var publishers = Enumerable.Range(13, 256).Select(tick => Task.Run(async () =>
        {
            await start.Task;
            try { await host.PublishSnapshotAsync(new(tick, []), timeout.Token); }
            catch (ArgumentException) { }
        })).ToArray();
        start.SetResult(); await Task.WhenAll(publishers);
        long previous = 12;
        while (previous != 268)
        {
            var next = await client.Snapshots.ReadAsync(timeout.Token);
            Check(next.Tick > previous && next.Enemies.Single().Hp == 0, "concurrent publication regressed tick or HP");
            previous = next.Tick;
        }
    }
}
