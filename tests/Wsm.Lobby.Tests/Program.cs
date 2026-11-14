using System.Net;
using Wsm.Lobby;

await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "Host", new LobbyRules());
await using var client = await LobbyClient.JoinAsync(server.Address, "Guest");
var snapshot = await client.GetStateAsync();
if (snapshot.Slots.Count != 2 || !snapshot.Slots.Any(x => x.PlayerId == client.PlayerId))
    throw new Exception("Joined player missing from snapshot");
Console.WriteLine("PASS join is visible over HTTP");
await ConnectionCases.Run();
await NetworkCases.Run();
await ReviewCases.Run();
