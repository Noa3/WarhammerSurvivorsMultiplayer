using System.Net;
using System.Text.Json;
using Wsm.Core;

if (JsonSerializer.IsReflectionEnabledByDefault)
    throw new InvalidOperationException("This regression must execute with reflection serialization disabled.");
using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
await using var host = new SessionHost(new SessionSettings(2, DeathMode.AllDead, 0, BotDifficulty.Normal), "fixture-only");
host.Start();
await using var client = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "fixture-only", timeout.Token);
while (host.ConnectedClients != 1) await Task.Delay(10, timeout.Token);
await host.PublishSnapshotAsync(new WorldSnapshot(1, [new WorldEntity("aot-wire-fixture", 3, 7, true)]), timeout.Token);
var snapshot = await client.Snapshots.ReadAsync(timeout.Token);
if (snapshot.Tick != 1 || snapshot.Entities is not [{ Id: "aot-wire-fixture", X: 3, Y: 7, Alive: true }])
    throw new InvalidDataException("Actual TCP snapshot changed during source-generated serialization.");
await Task.Delay(TimeSpan.FromSeconds(6), timeout.Token);
await host.PublishSnapshotAsync(new WorldSnapshot(2, [new WorldEntity("aot-wire-fixture", 5, 9, false)]), timeout.Token);
var second = await client.Snapshots.ReadAsync(timeout.Token);
if (second.Tick != 2 || second.Entities is not [{ X: 5, Y: 9, Alive: false }])
    throw new InvalidDataException("TCP snapshot delivery did not survive source-generated ping/pong.");
Console.WriteLine("PASS real TCP handshake, snapshots and heartbeat with reflection serialization disabled");
