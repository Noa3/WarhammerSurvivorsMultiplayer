using System.Net;
using Wsm.Lobby;

if (args is ["--idle-menu"]) { await IdleLobbyCases.Run(); return; }
if (args is ["--leave-failure"]) { await LeaveCleanupCases.FailedLeavesKeepAlive(); return; }
if (args is ["--leave-launch"]) { await LeaveCleanupCases.LeaveDuringLaunch(); return; }
if (args is ["--leave-fault"]) { await LeaveCleanupCases.FaultedHeartbeatDisposesHttp(); return; }
if (args.Length != 0) throw new ArgumentException("Supported filters: --idle-menu, --leave-failure, --leave-launch, --leave-fault");

await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "Host", new LobbyRules());
await using var client = await LobbyClient.JoinAsync(server.Address, "Guest");
var snapshot = await client.GetStateAsync();
if (snapshot.Slots.Count != 2 || !snapshot.Slots.Any(x => x.PlayerId == client.PlayerId))
    throw new Exception("Joined player missing from snapshot");
Console.WriteLine("PASS join is visible over HTTP");
await ConnectionCases.Run();
await NetworkCases.Run();
await ReviewCases.Run();
await PolicyCases.Run();
await IdleLobbyCases.Run();
await LeaveCleanupCases.FailedLeavesKeepAlive();
await LeaveCleanupCases.LeaveDuringLaunch();
await LeaveCleanupCases.FaultedHeartbeatDisposesHttp();
