using System.Net;
using System.Text;
using Wsm.Core;

internal static class PlayerNamesRegression
{
    public static async Task Run()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        await using var host = new SessionHost(new(2, DeathMode.AllDead, 0, BotDifficulty.Normal), "x", displayName: "Höst😀\n");
        host.Start();
        await using var a = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token, "同😀\nName");
        await using var b = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token, "同😀\nName");
        async Task Wait(Func<bool> predicate)
        {
            while (!predicate()) { timeout.Token.ThrowIfCancellationRequested(); await Task.Delay(10, timeout.Token); }
        }
        await Wait(() => a.PlayerNames.Length == 3 && b.PlayerNames.Length == 3 && host.PlayerNames.Length == 3);
        var roster = host.PlayerNames;
        Check(roster[0] == new PlayerName(host.HostPlayerId, "Höst😀"), "host name and identity");
        Check(roster.Single(p => p.PlayerId == a.PlayerId).Name == "同😀Name", "first guest identity");
        Check(roster.Single(p => p.PlayerId == b.PlayerId).Name == "同😀Name", "duplicate name has separate identity");
        Check(a.PlayerNames.SequenceEqual(roster) && b.PlayerNames.SequenceEqual(roster), "all peers see same deterministic roster");
        await b.DisposeAsync();
        await Wait(() => a.PlayerNames.Length == 2 && host.PlayerNames.Length == 2);
        Check(!a.PlayerNames.Any(p => p.PlayerId == b.PlayerId), "departed ID retained");
        await a.SendInputAsync(new(a.PlayerId, 0, 0), timeout.Token);
        Check((await host.Commands.ReadAsync(timeout.Token)).PlayerId == a.PlayerId, "control remains bound to assigned ID");
        var bounded = DisplayNames.Sanitize(string.Concat(Enumerable.Repeat("😀", 80)));
        Check(bounded.EnumerateRunes().Count() == 48 && Encoding.UTF8.GetByteCount(bounded) == 192, "scalar and byte bounds");
        Check(DisplayNames.Sanitize("\r\n\t ") == "Player", "blank fallback");
        await PlayerNamesWireRegression.Run();
        Console.WriteLine("PASS named TCP roster, duplicate identities, removal and bounds");
    }
    private static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }
}
