using System.Net;
using System.Net.Sockets;
using System.Text;
using Wsm.Core;

internal static class PlayerNamesWireRegression
{
    public static async Task Run()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        var local = Guid.NewGuid();
        var host = Guid.NewGuid();
        string entry = $"{{\"PlayerId\":\"{local}\",\"Name\":\"Player\"}}";
        string[] invalid = [
            "null", "[]", "[null]", "[{}]",
            $"[{entry},{entry}]",
            $"[{{\"PlayerId\":\"{Guid.Empty}\",\"Name\":\"Player\"}},{entry}]",
            $"[{{\"PlayerId\":\"{host}\",\"Name\":\"bad\\nname\"}},{entry}]",
            $"[{{\"PlayerId\":\"{host}\",\"Name\":\"Player\",\"Name\":\"Other\"}},{entry}]",
            $"[{{\"PlayerId\":\"{host}\",\"Name\":\"Player\"}}]",
            $"[{entry},{string.Join(',', Enumerable.Range(0, 2).Select(_ => $"{{\"PlayerId\":\"{Guid.NewGuid()}\",\"Name\":\"Player\"}}"))}]"
        ];
        foreach (var array in invalid) await InvalidRoster(local, array, deadline.Token);
        await VersionRejection(deadline.Token);
        Console.WriteLine("PASS malformed roster and protocol 4/5 incompatibility");
    }
    private static async Task InvalidRoster(Guid id, string roster, CancellationToken token)
    {
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        try
        {
            var connecting = SessionClient.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, "x", token);
            using var tcp = await listener.AcceptTcpClientAsync(token); using var peer = new Peer(tcp);
            await peer.ReadAsync(token);
            await peer.SendAsync(new Message("welcome", Version: SessionHost.ProtocolVersion, PlayerId: id, Settings: new(1, DeathMode.AllDead, 0, BotDifficulty.Normal)), token);
            await using var client = await connecting;
            await peer.SendEncodedAsync(Encoding.UTF8.GetBytes($"{{\"Type\":\"roster\",\"Roster\":{roster}}}"), token);
            Exception? failure = null;
            try { await client.Snapshots.Completion.WaitAsync(token); } catch (Exception e) { failure = e; }
            if (failure is not InvalidDataException || client.PlayerNames.Length != 0) throw new Exception("malformed roster accepted: " + roster);
        }
        finally { listener.Stop(); }
    }
    private static async Task VersionRejection(CancellationToken token)
    {
        await using var server = new SessionHost(new(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); server.Start();
        using var tcp = new TcpClient(); await tcp.ConnectAsync(IPAddress.Loopback, server.Port, token); using var peer = new Peer(tcp);
        await peer.SendAsync(new Message("join", Version: 4, Password: "x"), token);
        var response = await peer.ReadAsync(token);
        if (response.Type != "reject" || response.Error is null || !response.Error.Contains("version", StringComparison.OrdinalIgnoreCase)) throw new Exception("v4 join not explicitly rejected");
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        try
        {
            var connecting = SessionClient.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, "x", token);
            using var oldTcp = await listener.AcceptTcpClientAsync(token); using var oldPeer = new Peer(oldTcp);
            await oldPeer.ReadAsync(token);
            await oldPeer.SendAsync(new Message("welcome", Version: 4, PlayerId: Guid.NewGuid(), Settings: new(1, DeathMode.AllDead, 0, BotDifficulty.Normal)), token);
            try { await connecting; throw new Exception("v4 welcome accepted"); }
            catch (InvalidDataException e) when (e.Message.Contains("version", StringComparison.OrdinalIgnoreCase)) { }
        }
        finally { listener.Stop(); }
    }
}
