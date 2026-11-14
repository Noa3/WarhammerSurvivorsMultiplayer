using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Wsm.Lobby;

internal static class IdleLobbyCases
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    public static async Task Run()
    {
        await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "menu-host", new());
        var idle = await LobbyClient.JoinAsync(server.Address, "reading-bestiary");
        using var silent = new HttpClient { BaseAddress = server.Address };
        using var joined = await silent.PostAsJsonAsync("/join", new { name = "silent-peer", protocolVersion = LobbyProtocol.Version });
        joined.EnsureSuccessStatusCode();
        using var body = JsonDocument.Parse(await joined.Content.ReadAsStringAsync());
        Guid staleId = body.RootElement.GetProperty("playerId").GetGuid();
        silent.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", body.RootElement.GetProperty("token").GetString());
        try
        {
            // No state reads, gameplay traffic, manual heartbeat or UI updates during this interval.
            await Task.Delay(TimeSpan.FromSeconds(19));
            var snapshot = server.Snapshot;
            Check(snapshot.Slots.Any(s => s.PlayerId == idle.PlayerId), "Idle lobby client must stay connected without UI polling while browsing menus.");
            Check(snapshot.Slots.All(s => s.PlayerId != staleId), "An actually silent guest must still expire.");
            Check((await idle.GetStateAsync()).Slots.Any(s => s.PlayerId == idle.PlayerId), "Idle client's admission must remain usable.");
            using var expired = await silent.GetAsync("/state");
            Check(expired.StatusCode == HttpStatusCode.Unauthorized, "Expired peer must not retain access.");
        }
        finally { await idle.DisposeAsync(); }
        Check(server.Snapshot.Slots.Count == 1, "Disposal must stop heartbeat and remove only the owned lobby slot.");
        await idle.DisposeAsync();
        Console.WriteLine("PASS idle HTTP lobby survives menu inactivity; silent peer expires; repeated disposal is safe");
    }
}
