using System.Net;
using System.Net.Sockets;
using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using Wsm.Core;

internal static class KeepaliveRegression
{
    public static async Task Idle()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(45));
        await using var host = new SessionHost(new SessionSettings(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x");
        host.Start();
        await using var client = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", deadline.Token);
        await host.Joined.ReadAsync(deadline.Token);
        await Task.Delay(TimeSpan.FromSeconds(32), deadline.Token);
        if (host.ConnectedClients != 1 || client.Snapshots.Completion.IsCompleted || client.Rewards.Completion.IsCompleted)
            throw new Exception("Healthy idle session disconnected before 32 seconds.");
        await client.SendInputAsync(new PlayerInput(client.PlayerId, 0, 0), deadline.Token);
        if ((await host.Commands.ReadAsync(deadline.Token)).PlayerId != client.PlayerId) throw new Exception("Idle client command failed.");
        await host.PublishSnapshotAsync(new WorldSnapshot(1, []), deadline.Token);
        if ((await client.Snapshots.ReadAsync(deadline.Token)).Tick != 1) throw new Exception("Idle client snapshot failed.");
        if (host.Commands.TryRead(out _) || client.Snapshots.TryRead(out _) || client.Rewards.TryRead(out _)) throw new Exception("Heartbeat leaked into application channels.");
        Console.WriteLine("PASS real TCP healthy idle >30s");
    }

    public static async Task Run()
    {
        // All real-time long-running cases execute concurrently, but every task is awaited.
        await Task.WhenAll(Idle(), BlockedContextRegression.Run(), SilentClient(), SilentHost(), Malformed(), Disposal());
    }

    private static async Task SilentClient()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(42));
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x");
        host.Start();
        using var tcp = new TcpClient();
        await tcp.ConnectAsync(IPAddress.Loopback, host.Port, deadline.Token);
        var stream = tcp.GetStream();
        await Send(stream, JsonSerializer.Serialize(new Message("join", Version: SessionHost.ProtocolVersion, Password: "x", DisplayName: "Player")), deadline.Token);
        await Read(stream, deadline.Token);
        var id = await host.Joined.ReadAsync(deadline.Token); await Read(stream, deadline.Token); // initial roster
        using var ping = JsonDocument.Parse(await Read(stream, deadline.Token));
        Check(ping.RootElement.GetProperty("Type").GetString() == "ping", "host did not send ping");
        var disconnected = host.Disconnected.ReadAsync(deadline.Token).AsTask();
        // Valid gameplay traffic must not substitute for a correlated pong.
        while (!disconnected.IsCompleted)
        {
            try { await Send(stream, JsonSerializer.Serialize(new Message("input", Input: new(id, 0, 0))), deadline.Token); }
            catch (IOException) { break; }
            await Task.WhenAny(disconnected, Task.Delay(500, deadline.Token));
        }
        Check(await disconnected == id, "silent client was not disconnected");
        Console.WriteLine("PASS host times out nonresponding client despite gameplay traffic");
    }

    private static async Task SilentHost()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(42));
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        try
        {
            var connect = SessionClient.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, "x", deadline.Token);
            using var tcp = await listener.AcceptTcpClientAsync(deadline.Token);
            var stream = tcp.GetStream(); await Read(stream, deadline.Token);
            await Send(stream, JsonSerializer.Serialize(new Message("welcome", Version: SessionHost.ProtocolVersion, PlayerId: Guid.NewGuid(), Settings: new(1, DeathMode.AllDead, 0, BotDifficulty.Normal))), deadline.Token);
            await using var client = await connect;
            using var ping = JsonDocument.Parse(await Read(stream, deadline.Token));
            Check(ping.RootElement.GetProperty("Type").GetString() == "ping", "client did not send ping");
            var completion = client.Rewards.Completion;
            long tick = 0;
            while (!completion.IsCompleted)
            {
                try { await Send(stream, JsonSerializer.Serialize(new Message("snapshot", Snapshot: new(tick++, []))), deadline.Token); }
                catch (IOException) { break; }
                await Task.WhenAny(completion, Task.Delay(500, deadline.Token));
            }
            try { await completion.WaitAsync(deadline.Token); } catch (Exception ex) when (ex is IOException or ObjectDisposedException or OperationCanceledException && !deadline.IsCancellationRequested) { }
            Check(completion.IsCompleted && !deadline.IsCancellationRequested, "silent host did not time out");
            Console.WriteLine("PASS client times out nonresponding host despite gameplay traffic");
        }
        finally { listener.Stop(); }
    }

    private static async Task Malformed()
    {
        var nonce = Guid.NewGuid();
        string[] invalid = ["{\"Type\":\"ping\"}", $"{{\"Type\":\"ping\",\"Nonce\":\"{nonce}\",\"Extra\":1}}", $"{{\"Type\":\"pong\",\"Nonce\":\"{nonce}\"}}", $"{{\"Type\":\"ping\",\"Nonce\":\"{nonce}\",\"Nonce\":\"{nonce}\"}}"];
        var cases = invalid.SelectMany(payload => new[] { RejectClient(payload, false), RejectHost(payload, false) }).ToList();
        cases.Add(RejectClient($"{{\"Type\":\"pong\",\"Nonce\":\"{nonce}\"}}", true));
        cases.Add(RejectHost($"{{\"Type\":\"pong\",\"Nonce\":\"{nonce}\"}}", true));
        cases.Add(CorrelationClient());
        cases.Add(CorrelationHost());
        await Task.WhenAll(cases);
        Console.WriteLine("PASS malformed, unsolicited and mismatched heartbeat in both directions");
    }
    private static async Task RejectClient(string payload, bool waitPing)
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(12));
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        using var tcp = new TcpClient(); await tcp.ConnectAsync(IPAddress.Loopback, host.Port, deadline.Token);
        var stream = tcp.GetStream();
        await Send(stream, JsonSerializer.Serialize(new Message("join", Version: SessionHost.ProtocolVersion, Password: "x", DisplayName: "Player")), deadline.Token);
        await Read(stream, deadline.Token); var id = await host.Joined.ReadAsync(deadline.Token); await Read(stream, deadline.Token); // initial roster
        if (waitPing) await Read(stream, deadline.Token);
        await Send(stream, payload, deadline.Token);
        Check(await host.Disconnected.ReadAsync(deadline.Token) == id, "invalid heartbeat client accepted");
        Check(!host.Commands.TryRead(out _), "heartbeat leaked into commands");
    }
    private static async Task RejectHost(string payload, bool waitPing)
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(12));
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        try
        {
            var connect = SessionClient.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, "x", deadline.Token);
            using var tcp = await listener.AcceptTcpClientAsync(deadline.Token); var stream = tcp.GetStream();
            await Read(stream, deadline.Token);
            await Send(stream, JsonSerializer.Serialize(new Message("welcome", Version: SessionHost.ProtocolVersion, PlayerId: Guid.NewGuid(), Settings: new(1, DeathMode.AllDead, 0, BotDifficulty.Normal))), deadline.Token);
            await using var client = await connect;
            if (waitPing) await Read(stream, deadline.Token);
            await Send(stream, payload, deadline.Token);
            Exception? error = null; try { await client.Snapshots.Completion.WaitAsync(deadline.Token); } catch (Exception ex) { error = ex; }
            Check(error is InvalidDataException, $"invalid heartbeat host accepted: {error}");
            Check(!client.Snapshots.TryRead(out _) && !client.Rewards.TryRead(out _), "heartbeat leaked into client channels");
        }
        finally { listener.Stop(); }
    }
    private static async Task CorrelationClient()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(12));
        await using var host = new SessionHost(new(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        using var tcp = new TcpClient(); await tcp.ConnectAsync(IPAddress.Loopback, host.Port, deadline.Token);
        var stream = tcp.GetStream();
        await Send(stream, JsonSerializer.Serialize(new Message("join", Version: SessionHost.ProtocolVersion, Password: "x", DisplayName: "Player")), deadline.Token);
        await Read(stream, deadline.Token); var id = await host.Joined.ReadAsync(deadline.Token); await Read(stream, deadline.Token); // initial roster
        await CheckEcho(stream, deadline.Token);
        using var ping = JsonDocument.Parse(await Read(stream, deadline.Token));
        var nonce = ping.RootElement.GetProperty("Nonce").GetGuid();
        var pong = JsonSerializer.Serialize(new { Type = "pong", Nonce = nonce });
        await Send(stream, pong, deadline.Token);
        // A valid application command proves the first pong was processed, before replaying it.
        await Send(stream, JsonSerializer.Serialize(new Message("input", Input: new(id, 0, 0))), deadline.Token);
        Check((await host.Commands.ReadAsync(deadline.Token)).PlayerId == id, "matching pong rejected");
        await Send(stream, pong, deadline.Token);
        Check(await host.Disconnected.ReadAsync(deadline.Token) == id, "replayed pong accepted by host");
    }
    private static async Task CorrelationHost()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(12));
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        try
        {
            var connect = SessionClient.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, "x", deadline.Token);
            using var tcp = await listener.AcceptTcpClientAsync(deadline.Token); var stream = tcp.GetStream();
            await Read(stream, deadline.Token);
            await Send(stream, JsonSerializer.Serialize(new Message("welcome", Version: SessionHost.ProtocolVersion, PlayerId: Guid.NewGuid(), Settings: new(1, DeathMode.AllDead, 0, BotDifficulty.Normal))), deadline.Token);
            await using var client = await connect;
            await CheckEcho(stream, deadline.Token);
            using var ping = JsonDocument.Parse(await Read(stream, deadline.Token));
            var nonce = ping.RootElement.GetProperty("Nonce").GetGuid();
            var pong = JsonSerializer.Serialize(new { Type = "pong", Nonce = nonce });
            await Send(stream, pong, deadline.Token);
            await Send(stream, JsonSerializer.Serialize(new Message("snapshot", Snapshot: new(1, []))), deadline.Token);
            Check((await client.Snapshots.ReadAsync(deadline.Token)).Tick == 1, "matching pong rejected");
            await Send(stream, pong, deadline.Token);
            Exception? error = null; try { await client.Snapshots.Completion.WaitAsync(deadline.Token); } catch (Exception ex) { error = ex; }
            Check(error is InvalidDataException, "replayed pong accepted by client");
        }
        finally { listener.Stop(); }
    }
    private static async Task CheckEcho(Stream stream, CancellationToken token)
    {
        var nonce = Guid.NewGuid();
        await Send(stream, JsonSerializer.Serialize(new { Type = "ping", Nonce = nonce }), token);
        using var response = JsonDocument.Parse(await Read(stream, token));
        Check(response.RootElement.GetProperty("Type").GetString() == "pong" && response.RootElement.GetProperty("Nonce").GetGuid() == nonce, "ping nonce not echoed");
    }
    private static async Task Disposal()
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(12));
        var host = new SessionHost(new(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        using var tcp = new TcpClient(); await tcp.ConnectAsync(IPAddress.Loopback, host.Port, deadline.Token);
        var stream = tcp.GetStream();
        await Send(stream, JsonSerializer.Serialize(new Message("join", Version: SessionHost.ProtocolVersion, Password: "x", DisplayName: "Player")), deadline.Token);
        await Read(stream, deadline.Token); await host.Joined.ReadAsync(deadline.Token); await Read(stream, deadline.Token); await Read(stream, deadline.Token);
        await Task.WhenAll(host.DisposeAsync().AsTask(), host.DisposeAsync().AsTask()).WaitAsync(deadline.Token);
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        try
        {
            var connect = SessionClient.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, "x", deadline.Token);
            using var server = await listener.AcceptTcpClientAsync(deadline.Token); var peer = server.GetStream();
            await Read(peer, deadline.Token);
            await Send(peer, JsonSerializer.Serialize(new Message("welcome", Version: SessionHost.ProtocolVersion, PlayerId: Guid.NewGuid(), Settings: new(1, DeathMode.AllDead, 0, BotDifficulty.Normal))), deadline.Token);
            var client = await connect; await Read(peer, deadline.Token);
            await Task.WhenAll(client.DisposeAsync().AsTask(), client.DisposeAsync().AsTask()).WaitAsync(deadline.Token);
        }
        finally { listener.Stop(); }
        Console.WriteLine("PASS concurrent disposal while heartbeat pending");
    }
    private static void Check(bool ok, string reason) { if (!ok) throw new Exception(reason); }
    private static async Task Send(Stream stream, string json, CancellationToken token)
    {
        var bytes = Encoding.UTF8.GetBytes(json); var header = new byte[4];
        BinaryPrimitives.WriteInt32BigEndian(header, bytes.Length);
        await stream.WriteAsync(header, token); await stream.WriteAsync(bytes, token);
    }
    private static async Task<string> Read(Stream stream, CancellationToken token)
    {
        var header = new byte[4]; await stream.ReadExactlyAsync(header, token);
        var bytes = new byte[BinaryPrimitives.ReadInt32BigEndian(header)];
        await stream.ReadExactlyAsync(bytes, token); return Encoding.UTF8.GetString(bytes);
    }
}
