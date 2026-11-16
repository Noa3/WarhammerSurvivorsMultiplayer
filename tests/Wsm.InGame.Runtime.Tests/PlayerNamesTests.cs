using System.Net;
using Wsm.Core;
using Wsm.InGame.Runtime;

internal static class PlayerNamesTests
{
    public static async Task Roster()
    {
        await using var host = new IngameSessionController();
        await using var guest = new IngameSessionController();
        var property = typeof(IngameSessionController).GetProperty("PlayerNames");
        if (property is null) throw new Exception("connected player roster is not exposed");
        if (await host.HostAsync(0, false, displayName: "Host😀") != TransitionResult.Started) throw new Exception("host admission failed");
        if (await guest.JoinAsync("127.0.0.1", host.Status.BoundPort, displayName: "Gast\n😀") != TransitionResult.Started) throw new Exception("guest admission failed");
        async Task WaitFor(Func<bool> predicate)
        {
            var until = DateTime.UtcNow.AddSeconds(5);
            while (!predicate()) { if (DateTime.UtcNow >= until) throw new Exception("roster did not converge"); await Task.Delay(20); }
        }
        string Names(IngameSessionController c) => (string)property.GetValue(c)!;
        await WaitFor(() => Names(host) == "Host😀\nGast😀" && Names(guest) == "Host😀\nGast😀");
        if (host.LocalPlayerName != "Host😀" || guest.LocalPlayerName != "Gast😀") throw new Exception("local names not frozen");
        await guest.LeaveAsync();
        await WaitFor(() => Names(host) == "Host😀" && Names(guest) == "");
        await using var survivor = new IngameSessionController();
        if (await survivor.JoinAsync("127.0.0.1", host.Status.BoundPort, displayName: "Survivor") != TransitionResult.Started) throw new Exception("second admission failed");
        await WaitFor(() => Names(survivor) == "Host😀\nSurvivor");
        await host.LeaveAsync();
        await WaitFor(() => Names(survivor) == "" && survivor.LocalPlayerName == "");
        Console.WriteLine("PASS connected roster over TCP");
        await LargeExternalRoster();
    }

    private static async Task LargeExternalRoster()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var host = new SessionHost(new(8, DeathMode.AllDead, 0, BotDifficulty.Normal), "", displayName: "Host");
        await using var runtime = new IngameSessionController();
        var extraPeers = new List<SessionClient>();
        try
        {
            host.Start();
            if (await runtime.JoinAsync("127.0.0.1", host.Port, deadline.Token, "Runtime") != TransitionResult.Started)
                throw new Exception("external host admission failed");
            for (var i = 1; i <= 7; i++)
                extraPeers.Add(await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "", deadline.Token, $"Peer{i}"));

            async Task WaitFor(Func<bool> predicate, string message)
            {
                while (!predicate())
                {
                    if (deadline.IsCancellationRequested) throw new TimeoutException(message);
                    await Task.Delay(20, deadline.Token);
                }
            }

            await WaitFor(() => host.PlayerNames.Length == 9 && runtime.PlayerNames.Split('\n').Length == 9, "large roster did not converge");
            var roster = host.PlayerNames;
            var peerIds = extraPeers.Select(p => p.PlayerId).ToHashSet();
            var runtimeIds = roster.Where(p => p.PlayerId != host.HostPlayerId && !peerIds.Contains(p.PlayerId)).Select(p => p.PlayerId).ToArray();
            if (runtimeIds.Length != 1 || roster[0].PlayerId != host.HostPlayerId || roster[0].Name != "Host")
                throw new Exception("host or runtime identity missing from roster");
            var expected = new Dictionary<Guid, string> { [host.HostPlayerId] = "Host", [runtimeIds[0]] = "Runtime" };
            for (var i = 0; i < extraPeers.Count; i++) expected.Add(extraPeers[i].PlayerId, $"Peer{i + 1}");
            var expectedNames = "Host\n" + string.Join("\n", expected.Where(p => p.Key != host.HostPlayerId).OrderBy(p => p.Key).Select(p => p.Value));
            if (roster.Any(p => !expected.TryGetValue(p.PlayerId, out var name) || p.Name != name) || runtime.PlayerNames != expectedNames)
                throw new Exception("large roster names or identity ordering differ");

            await Task.WhenAll(extraPeers.Select(p => p.DisposeAsync().AsTask()));
            await WaitFor(() => runtime.PlayerNames == "Host\nRuntime", "departed peers retained in roster");
            await host.DisposeAsync();
            await WaitFor(() => runtime.PlayerNames == "" && runtime.LocalPlayerName == "" && runtime.Status.Failure == IngameFailure.HostLost,
                "large roster retained after host loss");
            Console.WriteLine("PASS large external roster over TCP (host + 8 guests, peer departure and host loss)");
        }
        finally
        {
            await Task.WhenAll(extraPeers.Select(p => p.DisposeAsync().AsTask()));
        }
    }
}
