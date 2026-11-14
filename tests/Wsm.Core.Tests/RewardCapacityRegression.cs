using System.Collections;
using System.Diagnostics;
using System.Net;
using System.Reflection;
using Wsm.Core;

internal static class RewardCapacityRegression
{
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static (int Events, int Collections) Usage(SessionHost host) =>
        (((IDictionary)typeof(SessionHost).GetField("rewardEvents", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(host)!).Count,
         (int)typeof(SessionHost).GetField("rewardCollectionCount", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(host)!);
    private static void RefusesCapacity(Action action)
    {
        try { action(); }
        catch (InvalidOperationException ex) when (ex.Message == "Reward collection capacity reached.") { return; }
        throw new Exception("Expected collection capacity rejection.");
    }

    public static async Task Run()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(90));
        var clock = Stopwatch.StartNew();
        await using var host = new SessionHost(new(2, DeathMode.AllDead, 0, BotDifficulty.Normal), "fixture"); host.Start();
        await using var a = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "fixture", deadline.Token);
        await using var b = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "fixture", deadline.Token);
        await host.Joined.ReadAsync(deadline.Token); await host.Joined.ReadAsync(deadline.Token);
        const int events = 50_000;
        RewardReceipt? first = null;
        for (int offset = 0; offset < events; offset += 32)
        {
            int count = Math.Min(32, events - offset);
            for (int i = 0; i < count; i++)
            {
                var reward = new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Xp, 1);
                first ??= reward;
                host.PublishReward(a.PlayerId, reward);
                host.PublishReward(b.PlayerId, reward with { RecipientId = b.PlayerId });
            }
            for (int i = 0; i < count; i++)
            {
                await a.Rewards.ReadAsync(deadline.Token); await b.Rewards.ReadAsync(deadline.Token);
            }
            // Exercise the normal input path to keep the host's read deadline live.
            await a.SendInputAsync(new(a.PlayerId, 0, 0), deadline.Token);
            await b.SendInputAsync(new(b.PlayerId, 0, 0), deadline.Token);
            await host.Commands.ReadAsync(deadline.Token); await host.Commands.ReadAsync(deadline.Token);
        }
        var before = Usage(host);
        Check(before == (events, events * 2), "The public TCP reward path must reach the actual collection boundary.");
        Check(a.Progression.Xp == events && b.Progression.Xp == events, "Every admitted personal collection must be applied.");
        var rejected = new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Xp, 1);
        RefusesCapacity(() => host.PublishReward(a.PlayerId, rejected));
        Check(Usage(host) == before, "Rejected collection must not insert a phantom event or mutate ledger capacity.");
        RefusesCapacity(() => host.PublishReward(a.PlayerId, rejected with { Amount = 2 }));
        RefusesCapacity(() => host.PublishReward(b.PlayerId, rejected with { RecipientId = b.PlayerId }));
        Check(Usage(host) == before, "Changed retry after rejection must not bind event content or collector.");
        host.PublishReward(a.PlayerId, first!);
        host.PublishReward(b.PlayerId, first! with { RecipientId = b.PlayerId });
        Check(Usage(host) == before && a.Progression.Xp == events && b.Progression.Xp == events,
            "An admitted exact replay must remain harmless at capacity.");
        Console.WriteLine($"PASS actual TCP reward capacity: {before.Events} events / {before.Collections} collections, {clock.ElapsedMilliseconds} ms");
    }
}
