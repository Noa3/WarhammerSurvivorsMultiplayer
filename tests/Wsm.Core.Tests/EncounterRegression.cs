using System.Net;
using System.Net.Sockets;
using Wsm.Core;

static class EncounterRegression
{
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static void Throws<T>(Action action) where T : Exception { try { action(); } catch (T) { return; } throw new Exception("Expected " + typeof(T).Name); }
    public static async Task Run()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        await using var host = new SessionHost(new(3, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        await using var a = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await using var b = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token); await host.Joined.ReadAsync(timeout.Token);
        host.Encounter.RegisterEnemy("orc-1", 100);
        Throws<ArgumentException>(() => host.Encounter.RegisterEnemy("orc-1", 10));
        var hit1 = Guid.NewGuid();
        Check(!host.Encounter.ApplyValidatedHit(hit1, "orc-1", 30), "first hit must not kill");
        Check(!host.Encounter.ApplyValidatedHit(hit1, "orc-1", 30), "duplicate hit ignored");
        await host.PublishSnapshotAsync(new(1, []), timeout.Token);
        foreach (var client in new[] { a, b }) { var s = await client.Snapshots.ReadAsync(timeout.Token); Check(s.Enemies.Single() == new EnemyHealth("orc-1", 100, 70), "both clients see shared HP"); }
        Check(host.Encounter.ApplyValidatedHit(Guid.NewGuid(), "orc-1", 70), "second hit kills");
        Check(!host.Encounter.ApplyValidatedHit(Guid.NewGuid(), "orc-1", 1), "death only once");
        await host.PublishSnapshotAsync(new(2, []), timeout.Token);
        foreach (var client in new[] { a, b }) { var s = await client.Snapshots.ReadAsync(timeout.Token); Check(s.Enemies.Single().Hp == 0, "both clients see death"); }
        try { await host.PublishSnapshotAsync(new WorldSnapshot(3, []) { Enemies = [new("fake", 1, 1)] }, timeout.Token); throw new Exception("caller supplied HP accepted"); } catch (ArgumentException) { }
        var xp = new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Xp, 25);
        var item = new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Item, 1, "sword");
        var artifact = new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Artifact, 1, "relic");
        Throws<ArgumentException>(() => host.PublishReward(b.PlayerId, xp));
        var unknown = Guid.NewGuid();
        Throws<InvalidOperationException>(() => host.PublishReward(unknown, xp with { RecipientId = unknown }));
        host.PublishReward(a.PlayerId, xp); host.PublishReward(a.PlayerId, xp); host.PublishReward(a.PlayerId, item); host.PublishReward(a.PlayerId, artifact);
        for (int i = 0; i < 3; i++) await a.Rewards.ReadAsync(timeout.Token);
        Check(a.Progression.Xp == 25 && a.ReceivedRewards.Length == 3, "unique private rewards applied");
        Check(b.Progression.Xp == 0 && b.ReceivedRewards.Length == 0 && !b.Rewards.TryRead(out _), "other client unchanged");
        using var raw = new TcpClient(); await raw.ConnectAsync(IPAddress.Loopback, host.Port, timeout.Token);
        using var peer = new Peer(raw);
        await peer.SendAsync(new Message("join", Version: SessionHost.ProtocolVersion, Password: "x", DisplayName: "Player"), timeout.Token); await peer.ReadAsync(timeout.Token); await peer.ReadAsync(timeout.Token); // initial roster
        await peer.SendAsync(new Message("reward", Reward: xp), timeout.Token);
        try { await peer.ReadAsync(timeout.Token); throw new Exception("client reward frame accepted"); } catch (EndOfStreamException) { }
        Check(a.Progression.Xp == 25 && b.Progression.Xp == 0, "spoof did not alter rewards");
        await SpoofedHostRecipient(timeout.Token);
        await ClientOverflow(timeout.Token);
    }
    private static async Task SpoofedHostRecipient(CancellationToken token)
    {
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        try
        {
            var connecting = SessionClient.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, "x", token);
            using var tcp = await listener.AcceptTcpClientAsync(token);
            using var peer = new Peer(tcp);
            await peer.ReadAsync(token);
            var id = Guid.NewGuid();
            await peer.SendAsync(new Message("welcome", Version: SessionHost.ProtocolVersion, PlayerId: id, Settings: new(1, DeathMode.AllDead, 0, BotDifficulty.Normal)), token);
            await using var client = await connecting;
            await peer.SendAsync(new Message("reward", Reward: new(Guid.NewGuid(), Guid.NewGuid(), RewardKind.Xp, 7)), token);
            Exception? failure = null;
            try { await client.Snapshots.Completion.WaitAsync(token); } catch (Exception ex) { failure = ex; }
            Check(failure is InvalidDataException && client.Progression.Xp == 0 && client.ReceivedRewards.Length == 0, "client rejected wrong recipient");
        }
        finally { listener.Stop(); }
    }
    private static async Task ClientOverflow(CancellationToken token)
    {
        await using var host = new SessionHost(new(2, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        await using var client = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", token);
        await host.Joined.ReadAsync(token);
        for (int i = 0; i < 129; i++)
        {
            host.PublishReward(client.PlayerId, new(Guid.NewGuid(), client.PlayerId, RewardKind.Item, 1, "item"));
            while (client.ReceivedRewards.Length < i + 1) await Task.Delay(1, token);
        }
        Exception? failure = null;
        try { await client.Snapshots.Completion.WaitAsync(token); } catch (Exception ex) { failure = ex; }
        Check(failure is InvalidDataException && failure.Message.Contains("Reward channel full"), "client overflow must fail clearly");
        Check(client.ReceivedRewards.Length == 129, "overflow receipt remains in private store");
    }
}
