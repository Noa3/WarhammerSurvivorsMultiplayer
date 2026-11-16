using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using System.Text.Json.Nodes;
using Wsm.Contracts;
using Wsm.Core;

internal static class PolicyRegression
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Throws<T>(Action action) where T : Exception { try { action(); } catch (T) { return; } throw new Exception("Expected " + typeof(T).Name); }

    public static async Task TeamXp()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        var settings = new SessionSettings(3, DeathMode.AllDead, 0, BotDifficulty.Normal) { Policy = new CoopPolicy { ExperienceSharing = ExperienceSharing.Team, PickupSharing = PickupSharing.Team } };
        await using var host = new SessionHost(settings, "x"); host.Start();
        await using var a = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await using var b = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token); await host.Joined.ReadAsync(timeout.Token);
        Check(a.Settings.Policy == settings.Policy && b.Settings.Policy == settings.Policy, "policy did not roundtrip");
        var xp = new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Xp, 25);
        host.PublishReward(a.PlayerId, xp);
        await a.Rewards.ReadAsync(timeout.Token);
        using var shortWait = new CancellationTokenSource(TimeSpan.FromSeconds(2));
        await b.Rewards.ReadAsync(shortWait.Token);
        Check(a.Progression.Xp == 25 && b.Progression.Xp == 25, "team XP must grant full personal amounts");
        host.PublishReward(a.PlayerId, xp);
        Throws<ArgumentException>(() => host.PublishReward(b.PlayerId, xp with { RecipientId = b.PlayerId }));
        Throws<ArgumentException>(() => host.PublishReward(a.PlayerId, xp with { Amount = 26 }));
        await using var c = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token);
        host.PublishReward(a.PlayerId, xp);
        var item = new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Item, 1, "weapon");
        var artifact = new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Artifact, 1, "relic");
        host.PublishReward(a.PlayerId, item); host.PublishReward(a.PlayerId, artifact);
        await a.Rewards.ReadAsync(timeout.Token); await a.Rewards.ReadAsync(timeout.Token);
        foreach (var peer in new[] { b, c })
        {
            var barrier = new RewardReceipt(Guid.NewGuid(), peer.PlayerId, RewardKind.Item, 1, "barrier");
            host.PublishReward(peer.PlayerId, barrier);
            Check(await peer.Rewards.ReadAsync(timeout.Token) == barrier, "private reward or replay leaked before barrier");
        }
        Check(b.ReceivedRewards.Length == 2 && c.ReceivedRewards.Length == 1 && c.Progression.Xp == 0, "private rewards or replay leaked");
        foreach (var reward in new[] { new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Coins, 5), new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Consumable, 2, "potion") })
        {
            host.PublishReward(a.PlayerId, reward);
            foreach (var peer in new[] { a, b, c })
            {
                var copy = await peer.Rewards.ReadAsync(timeout.Token);
                Check(copy == reward with { RecipientId = peer.PlayerId }, "shared pickup copy mismatch");
            }
        }
        Throws<ArgumentException>(() => host.PublishReward(a.PlayerId, new(Guid.NewGuid(), a.PlayerId, RewardKind.Coins, 1, "bad")));
        Throws<ArgumentException>(() => host.PublishReward(a.PlayerId, new(Guid.NewGuid(), a.PlayerId, RewardKind.Consumable, 1)));
    }

    public static async Task PersonalXp()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        await using var host = new SessionHost(new(2, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        await using var a = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await using var b = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token); await host.Joined.ReadAsync(timeout.Token);
        var xp = new RewardReceipt(Guid.NewGuid(), a.PlayerId, RewardKind.Xp, 7);
        host.PublishReward(a.PlayerId, xp);
        host.PublishReward(b.PlayerId, xp with { RecipientId = b.PlayerId });
        host.PublishReward(b.PlayerId, xp with { RecipientId = b.PlayerId });
        await a.Rewards.ReadAsync(timeout.Token); await b.Rewards.ReadAsync(timeout.Token);
        Throws<ArgumentException>(() => host.PublishReward(b.PlayerId, xp with { RecipientId = b.PlayerId, Amount = 8 }));
        Check(a.Progression.Xp == 7 && b.Progression.Xp == 7, "personal event must be independently consumed exactly once");
    }

    public static async Task MalformedSettings()
    {
        var valid = JsonSerializer.SerializeToNode(new SessionSettings(1, DeathMode.AllDead, 0, BotDifficulty.Normal))!;
        List<Action<JsonNode>> mutations =
        [
            s => s.AsObject().Remove("Policy"),
            s => s["Policy"] = null,
            s => s["Policy"] = new JsonObject(),
            s => s["Policy"]!["ExperienceSharing"] = 99,
            s => s["Policy"]!["ExperienceDropLayout"] = 99,
            s => s["Policy"]!["PickupSharing"] = 99,
            s => s["Policy"]!["RecoveryPauseSeconds"] = 121,
            s => s["Policy"]!["DisconnectContinuation"] = 99,
            s => { s["Policy"]!["ExperienceSharing"] = 1; s["Policy"]!["ExperienceDropLayout"] = 1; }
        ];
        foreach (string field in valid["Policy"]!.AsObject().Select(p => p.Key))
            mutations.Add(s => s["Policy"]!.AsObject().Remove(field));
        foreach (string field in new[] { "EnemyPositionsSynchronized", "EnemyHealthSynchronized", "IndependentLevelChoices" })
            mutations.Add(s => s["Policy"]![field] = false);
        foreach (var mutate in mutations)
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
            try
            {
                var connecting = SessionClient.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, "x", timeout.Token);
                using var tcp = await listener.AcceptTcpClientAsync(timeout.Token);
                using var peer = new Peer(tcp); await peer.ReadAsync(timeout.Token);
                var settings = valid.DeepClone(); mutate(settings);
                var payload = JsonSerializer.SerializeToNode(new Message("welcome", Version: SessionHost.ProtocolVersion, PlayerId: Guid.NewGuid()))!;
                payload["Settings"] = settings;
                await peer.SendEncodedAsync(System.Text.Encoding.UTF8.GetBytes(payload.ToJsonString()), timeout.Token);
                try { await using var ignored = await connecting; throw new Exception("Malformed policy accepted: " + settings.ToJsonString()); }
                catch (Exception ex) when (ex is ArgumentException or InvalidDataException) { }
            }
            finally { listener.Stop(); }
        }
        Throws<ArgumentException>(() => new SessionHost(new SessionSettings(1, DeathMode.AllDead, 0, BotDifficulty.Normal) { Policy = new CoopPolicy { RecoveryPauseSeconds = 121 } }, "x"));
    }

    public static async Task PartialEnqueue()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        var settings = new SessionSettings(3, DeathMode.AllDead, 0, BotDifficulty.Normal) { Policy = new CoopPolicy { ExperienceSharing = ExperienceSharing.Team } };
        await using var host = new SessionHost(settings, "x"); host.Start();
        using var slow = new TcpClient { ReceiveBufferSize = 1024 };
        await slow.ConnectAsync(IPAddress.Loopback, host.Port, timeout.Token);
        using var slowPeer = new Peer(slow);
        await slowPeer.SendAsync(new Message("join", Version: SessionHost.ProtocolVersion, Password: "x", DisplayName: "Player"), timeout.Token);
        var slowId = (await slowPeer.ReadAsync(timeout.Token)).PlayerId;
        await host.Joined.ReadAsync(timeout.Token);
        await using var healthy = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token);
        var entities = Enumerable.Range(0, 1100).Select(_ => new WorldEntity(new string('x', 128), 0, 0, true)).ToArray();
        for (int i = 0; i < 160; i++) { await host.PublishSnapshotAsync(new(i, entities), timeout.Token); await Task.Delay(8, timeout.Token); }
        var status = host.OutboundStatus(slowId); await Task.Delay(250, timeout.Token);
        var after = host.OutboundStatus(slowId);
        Check(status.Writing && after.Writing && status.Sent == after.Sent, "slow TCP peer was not blocked");
        for (int i = 0; i < 128; i++) host.PublishReward(slowId, new(Guid.NewGuid(), slowId, RewardKind.Item, 1, "item"));
        var xp = new RewardReceipt(Guid.NewGuid(), healthy.PlayerId, RewardKind.Xp, 13);
        Throws<InvalidOperationException>(() => host.PublishReward(healthy.PlayerId, xp));
        Check((await healthy.Rewards.ReadAsync(timeout.Token)).Amount == 13 && healthy.Progression.Xp == 13, "healthy planned recipient missed partial enqueue");
        Check(await host.Disconnected.ReadAsync(timeout.Token) == slowId, "slow peer not disconnected");
        await using var newcomer = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token);
        Throws<InvalidOperationException>(() => host.PublishReward(healthy.PlayerId, xp));
        Throws<ArgumentException>(() => host.PublishReward(newcomer.PlayerId, xp with { RecipientId = newcomer.PlayerId }));
        var barrier = new RewardReceipt(Guid.NewGuid(), newcomer.PlayerId, RewardKind.Item, 1, "barrier");
        host.PublishReward(newcomer.PlayerId, barrier);
        Check(await newcomer.Rewards.ReadAsync(timeout.Token) == barrier, "retry redirected to newcomer");
        Check(healthy.Progression.Xp == 13 && newcomer.Progression.Xp == 0 && newcomer.ReceivedRewards.Length == 1, "retry duplicated or redirected a receipt");
    }
}
