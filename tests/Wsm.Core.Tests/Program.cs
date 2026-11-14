using System.Net;
using Wsm.Core;

if (args is ["--send-context"]) { await SendContextRegression.Run(); return; }
if (args is ["--reward-capacity"]) { await RewardCapacityRegression.Run(); return; }
if (args is ["--keepalive-context"]) { await BlockedContextRegression.Run(); return; }
if (args is ["--keepalive-idle"]) { await KeepaliveRegression.Idle(); return; }
if (args is ["--keepalive"]) { await KeepaliveRegression.Run(); return; }
if (args is ["--incoming-oversize"]) { await IncomingOversize(); return; }
if (args.Length != 0) throw new ArgumentException("Supported filters: --send-context, --reward-capacity, --keepalive-context, --keepalive-idle, --keepalive, --incoming-oversize");

var player = Guid.NewGuid();
var progress = new LocalProgression(player);
var receipt = Guid.NewGuid();
Check(progress.ApplyXp(receipt, player, 100), "first XP receipt");
Check(!progress.ApplyXp(receipt, player, 100), "duplicate XP receipt");
Check(progress.Xp == 100, "XP must not double");
progress.ChooseUpgrade(2, "power");
Check(progress.Choices[2] == "power", "personal level choice");
Throws<ArgumentException>(() => progress.ChooseUpgrade(2, "other"));
Console.WriteLine("PASS progression");

using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
await using var host = new SessionHost(new SessionSettings(2, DeathMode.AllDead, 1, BotDifficulty.Normal), "secret");
host.Start();
await using var client = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "secret", timeout.Token);
Check(client.Settings.BotCount == 1, "settings transmitted");
Check(await host.Joined.ReadAsync(timeout.Token) == client.PlayerId, "join notification");
await using (var second = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "secret", timeout.Token))
{
    Check(await host.Joined.ReadAsync(timeout.Token) == second.PlayerId, "second client joined");
    Check(client.Progression.ApplyXp(Guid.NewGuid(), client.PlayerId, 25), "personal XP applied");
    Check(second.Progression.Xp == 0, "XP is not shared");
    await second.DisposeAsync();
    Check(await host.Disconnected.ReadAsync(timeout.Token) == second.PlayerId, "second client cleanup");
}
await client.SendInputAsync(new PlayerInput(client.PlayerId, 0.5f, -0.5f), timeout.Token);
var command = await host.Commands.ReadAsync(timeout.Token);
Check(command.Input?.PlayerId == client.PlayerId, "input owner");
await host.PublishSnapshotAsync(new WorldSnapshot(1, [new WorldEntity("enemy", 1, 2, true)]), timeout.Token);
var snapshot = await client.Snapshots.ReadAsync(timeout.Token);
Check(snapshot.Tick == 1 && snapshot.Entities[0].X == 1, "snapshot roundtrip");
Console.WriteLine("PASS TCP session");

var other = Guid.NewGuid();
var state = new SessionState(DeathMode.AllDead);
state.AddPlayer(player); state.AddPlayer(other); state.MarkDead(player);
Check(state.IsSpectating(player) && !state.RunEnded, "spectator until all dead");
state.MarkDead(other); Check(state.RunEnded, "all dead ends run");
var immediate = new SessionState(DeathMode.AnyDeathEndsRun);
immediate.AddPlayer(player); immediate.AddPlayer(other); immediate.MarkDead(player);
Check(immediate.RunEnded, "any death ends run");
for (int i = 0; i < 200; i++)
{
    var move = BotPolicy.Move(player, i, BotDifficulty.Hard);
    Check(move == BotPolicy.Move(player, i, BotDifficulty.Hard) && move.X * move.X + move.Y * move.Y <= 1, "bounded deterministic bot");
}
Check(BotPolicy.Move(player, 0, BotDifficulty.Easy) != BotPolicy.Move(player, 0, BotDifficulty.Hard), "bot difficulty");
Throws<ArgumentException>(() => progress.ApplyXp(Guid.NewGuid(), other, 1));
Throws<ArgumentException>(() => client.SendInputAsync(new PlayerInput(other, 0, 0)));
Throws<ArgumentException>(() => client.SendInputAsync(new PlayerInput(client.PlayerId, float.NaN, 0)));
await client.SendUpgradeAsync(new UpgradeChoice(client.PlayerId, 2, "power"), timeout.Token);
var upgrade = await host.Commands.ReadAsync(timeout.Token);
Check(upgrade.Upgrade?.Choice == "power" && upgrade.PlayerId == client.PlayerId, "upgrade routing");
try { await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "wrong", timeout.Token); throw new Exception("password accepted"); }
catch (InvalidDataException) { }
await using (var raw = await OpenRaw(host.Port, timeout.Token))
{
    await SendRaw(raw, "{\"Type\":\"join\",\"Version\":999,\"Password\":\"secret\"}", timeout.Token);
    Check((await ReadRaw(raw, timeout.Token)).Contains("reject", StringComparison.Ordinal), "version rejection");
}
await using (var raw = await OpenRaw(host.Port, timeout.Token))
{
    await SendRaw(raw, "{\"Type\":\"join\",\"Version\":4,\"Password\":\"secret\"}", timeout.Token);
    var welcome = await ReadRaw(raw, timeout.Token);
    Check(welcome.Contains("welcome", StringComparison.Ordinal), "raw welcome");
    await SendRaw(raw, "{\"Type\":\"snapshot\"}", timeout.Token);
    Check(await raw.ReadAsync(new byte[1], timeout.Token) == 0, "client snapshot rejected");
}
await using (var raw = await OpenRaw(host.Port, timeout.Token))
{
    await SendRaw(raw, "{\"Type\":\"join\",\"Version\":4,\"Password\":\"secret\"}", timeout.Token);
    _ = await ReadRaw(raw, timeout.Token);
    await SendRaw(raw, $"{{\"Type\":\"input\",\"Input\":{{\"PlayerId\":\"{client.PlayerId}\",\"X\":0,\"Y\":0}}}}", timeout.Token);
    Check(await raw.ReadAsync(new byte[1], timeout.Token) == 0, "spoofed owner rejected");
    Check(!host.Commands.TryRead(out _), "spoofed input not delivered");
}

await client.DisposeAsync();
Guid disconnected;
do { disconnected = await host.Disconnected.ReadAsync(timeout.Token); } while (disconnected != client.PlayerId);
Check(!host.ConnectedPlayerIds.Contains(client.PlayerId), "disconnect cleanup");
Console.WriteLine("PASS rules and protocol rejection");

var failures = new List<string>();
foreach (var (name, test) in new (string, Func<Task>)[] { ("oversize incoming frame", IncomingOversize), ("oversize without peers", Oversize), ("malformed host JSON", MalformedHost), ("network failure regressions", NetworkRegression.Run), ("shared enemy health contract", SharedEnemyContract), ("shared encounter and private rewards", EncounterRegression.Run), ("snapshot encounter capacity", SnapshotSafetyRegression.Capacity), ("snapshot tick ordering", SnapshotSafetyRegression.TickOrder), ("team XP and replay", PolicyRegression.TeamXp), ("personal XP collection", PolicyRegression.PersonalXp), ("malformed policy", PolicyRegression.MalformedSettings), ("team partial enqueue retry", PolicyRegression.PartialEnqueue), ("reward capacity atomicity", RewardCapacityRegression.Run), ("keepalive TCP regressions", KeepaliveRegression.Run) })
{
    try { await test(); Console.WriteLine("PASS " + name); }
    catch (Exception ex) { failures.Add(name + ": " + ex); Console.WriteLine("FAIL " + failures[^1]); }
}
if (failures.Count != 0) throw new Exception($"{failures.Count} regression(s) failed");

static Task SharedEnemyContract()
{
    var json = System.Text.Json.JsonSerializer.Serialize(new WorldSnapshot(1, []));
    Check(json.Contains("\"Enemies\"", StringComparison.Ordinal), "snapshot must carry shared enemy health state");
    return Task.CompletedTask;
}

static async Task IncomingOversize()
{
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
    // A previous rejected peer can still hold a slot after its EOF. A fresh host
    // makes this a frame-length rejection, not an earlier capacity rejection.
    await using var host = new SessionHost(new SessionSettings(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x");
    host.Start();
    await using var raw = await OpenRaw(host.Port, timeout.Token);
    var length = new byte[4]; System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(length, 262145);
    await raw.WriteAsync(length, timeout.Token);
    Check(await raw.ReadAsync(new byte[1], timeout.Token) == 0, "oversize frame rejected");
    Check(host.ConnectedClients == 0 && !host.Commands.TryRead(out _), "oversize peer not admitted");
}

static async Task Oversize()
{
    await using var host = new SessionHost(new SessionSettings(1, DeathMode.AllDead, 0, BotDifficulty.Normal), "x");
    host.Start();
    var snapshot = new WorldSnapshot(1, Enumerable.Range(0, 2048).Select(_ => new WorldEntity(new string('x', 128), 0, 0, true)).ToArray());
    try { await host.PublishSnapshotAsync(snapshot); } catch (InvalidDataException) { return; }
    throw new Exception("Oversize snapshot accepted with no peers");
}

static async Task MalformedHost()
{
    await InvalidHostFrame("{");
    await InvalidHostFrame("{\"Type\":\"snapshot\",\"Snapshot\":{\"Tick\":-1,\"Entities\":[]}}");
    await InvalidHostFrame(null);
}
static async Task InvalidHostFrame(string? payload)
{
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    var listener = new System.Net.Sockets.TcpListener(IPAddress.Loopback, 0); listener.Start();
    try
    {
        var connect = SessionClient.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, "x", timeout.Token);
        using var tcp = await listener.AcceptTcpClientAsync(timeout.Token);
        var stream = tcp.GetStream();
        _ = await ReadRaw(stream, timeout.Token);
        await SendRaw(stream, $"{{\"Type\":\"welcome\",\"Version\":4,\"PlayerId\":\"{Guid.NewGuid()}\",\"Settings\":{{\"MaxClients\":1,\"DeathMode\":0,\"BotCount\":0,\"BotDifficulty\":1,\"Policy\":{System.Text.Json.JsonSerializer.Serialize(new Wsm.Contracts.CoopPolicy())}}}}}", timeout.Token);
        var client = await connect;
        if (payload is null) { var header = new byte[4]; System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(header, 262145); await stream.WriteAsync(header, timeout.Token); }
        else await SendRaw(stream, payload, timeout.Token);
        Exception? error = null;
        try { await client.Snapshots.Completion.WaitAsync(timeout.Token); } catch (Exception ex) { error = ex; }
        Exception? disposeError = null;
        try { await client.DisposeAsync(); } catch (Exception ex) { disposeError = ex; }
        Check(error is InvalidDataException && disposeError is null, $"Expected channel InvalidDataException and clean disposal, got channel={error?.GetType().Name ?? "none"}, dispose={disposeError?.GetType().Name ?? "none"}");
    }
    finally { listener.Stop(); }
}

static void Throws<T>(Action action) where T : Exception
{
    try { action(); } catch (T) { return; }
    throw new Exception("Expected " + typeof(T).Name);
}
static async Task<System.Net.Sockets.NetworkStream> OpenRaw(int port, CancellationToken token)
{
    var tcp = new System.Net.Sockets.TcpClient();
    await tcp.ConnectAsync(IPAddress.Loopback, port, token);
    return new System.Net.Sockets.NetworkStream(tcp.Client, ownsSocket: true);
}
static async Task SendRaw(System.IO.Stream stream, string json, CancellationToken token)
{
    var bytes = System.Text.Encoding.UTF8.GetBytes(json);
    var header = new byte[4]; System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(header, bytes.Length);
    await stream.WriteAsync(header, token); await stream.WriteAsync(bytes, token);
}
static async Task<string> ReadRaw(System.IO.Stream stream, CancellationToken token)
{
    var header = new byte[4]; await stream.ReadExactlyAsync(header, token);
    var bytes = new byte[System.Buffers.Binary.BinaryPrimitives.ReadInt32BigEndian(header)];
    await stream.ReadExactlyAsync(bytes, token); return System.Text.Encoding.UTF8.GetString(bytes);
}

static void Check(bool value, string message) { if (!value) throw new Exception(message); }
