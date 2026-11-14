using System.Net;
using System.Text.Json;
using System.Text.Json.Nodes;
using Wsm.Contracts;
using Wsm.Lobby;

static class PolicyCases
{
    private static LobbyRules Rules(string policy)
    {
        // Construct local host settings with defaults; raw wire omissions are tested separately.
        var supplied = JsonNode.Parse(policy);
        if (supplied is null) return new() { Policy = null! };
        var complete = JsonSerializer.SerializeToNode(new CoopPolicy())!;
        foreach (var pair in supplied.AsObject()) complete[pair.Key] = pair.Value?.DeepClone();
        return new() { Policy = complete.Deserialize<CoopPolicy>()! };
    }
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    public static async Task Run()
    {
        await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "policy-host", new());
        await using var guest = await LobbyClient.JoinAsync(server.Address, "policy-guest");
        var initial = JsonSerializer.SerializeToElement((await guest.GetStateAsync()).Rules);
        Check(initial.TryGetProperty("Policy", out var defaults), "Lobby response must carry the co-op policy, not silently ignore it.");
        Check(defaults.GetProperty("ExperienceSharing").GetInt32() == 0 && defaults.GetProperty("ExperienceDropLayout").GetInt32() == 0 && defaults.GetProperty("PickupSharing").GetInt32() == 0, "Default is personal rewards at synchronized drop positions.");
        Check(defaults.GetProperty("EnemyPositionsSynchronized").GetBoolean() && defaults.GetProperty("EnemyHealthSynchronized").GetBoolean() && defaults.GetProperty("IndependentLevelChoices").GetBoolean(), "Shared world and individual choices are invariants.");
        server.SetHostLoadout("fixture-host", ["fixture-weapon"]);
        await guest.SetLoadoutAsync("fixture-guest", ["fixture-weapon"]);
        server.SetHostReady(true); await guest.SetReadyAsync(true);
        server.Configure(Rules("{\"ExperienceSharing\":1,\"ExperienceDropLayout\":0,\"PickupSharing\":1,\"RecoveryPauseSeconds\":45,\"DisconnectContinuation\":1}"));
        var changed = await guest.GetStateAsync();
        var shared = JsonSerializer.SerializeToElement(changed.Rules).GetProperty("Policy");
        Check(shared.GetProperty("ExperienceSharing").GetInt32() == 1 && shared.GetProperty("PickupSharing").GetInt32() == 1 && shared.GetProperty("RecoveryPauseSeconds").GetInt32() == 45 && shared.GetProperty("DisconnectContinuation").GetInt32() == 1, "Guest must observe every host policy field.");
        Check(changed.Slots.All(s => !s.Ready), "Policy changes must invalidate human readiness.");
        long version = changed.Version;
        foreach (var invalid in new[] { "null", "{\"ExperienceSharing\":1,\"ExperienceDropLayout\":1}", "{\"ExperienceSharing\":88}", "{\"PickupSharing\":88}", "{\"ExperienceDropLayout\":88}", "{\"DisconnectContinuation\":88}", "{\"RecoveryPauseSeconds\":-1}", "{\"RecoveryPauseSeconds\":121}" })
        {
            bool refused = false;
            try { server.Configure(Rules(invalid)); } catch (ArgumentException) { refused = true; }
            Check(refused && server.Snapshot.Version == version, "Invalid or contradictory policy must be rejected without state mutation: " + invalid);
        }
        server.Configure(Rules("{\"ExperienceSharing\":0,\"ExperienceDropLayout\":1}"));
        Check(JsonSerializer.SerializeToElement((await guest.GetStateAsync()).Rules).GetProperty("Policy").GetProperty("ExperienceDropLayout").GetInt32() == 1, "Personal XP supports personal drop layouts.");
        Console.WriteLine("PASS host co-op rules over HTTP, readiness reset, invalid combinations and fixed shared world");
    }
}
