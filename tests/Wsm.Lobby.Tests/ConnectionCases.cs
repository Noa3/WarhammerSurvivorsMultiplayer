using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Wsm.Lobby;

static class ConnectionCases
{
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    public static async Task Run()
    {
        await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "feedback-host", new());
        using var raw = new HttpClient { BaseAddress = server.Address };
        foreach (var request in new object[] { new { name = "wrong-version", protocolVersion = 999 }, new { name = "missing-version" } })
        {
            using var response = await raw.PostAsJsonAsync("/join", request);
            Check((int)response.StatusCode == 426, "An incompatible/missing lobby protocol must be explicitly rejected before occupying a slot.");
            using var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            Check(body.RootElement.GetProperty("code").GetString() == "protocol_mismatch" && server.Snapshot.Slots.Count == 1, "Protocol mismatch must have a stable reason and leave the lobby unchanged.");
        }
        await using var guest = await LobbyClient.JoinAsync(server.Address, "feedback-guest");
        await guest.LeaveAsync();
        await ExpectCode(() => guest.GetStateAsync(), "session_expired", HttpStatusCode.Unauthorized);
        await using var full = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "full-host", new(3));
        await ExpectCode(() => LobbyClient.JoinAsync(full.Address, "extra"), "lobby_full", HttpStatusCode.Conflict);
        await InvalidWelcome();
        Console.WriteLine("PASS connection feedback: protocol mismatch, full lobby, expired session and malformed server");
    }
    private static async Task ExpectCode(Func<Task> action, string code, HttpStatusCode? status = null)
    {
        try { await action(); }
        catch (HttpRequestException ex)
        {
            Check(ex.GetType().GetProperty("Code")?.GetValue(ex) as string == code, "Missing structured failure code: " + code);
            if (status is not null) Check(ex.StatusCode == status, "HTTP status must be preserved.");
            return;
        }
        throw new Exception("Expected connection failure: " + code);
    }
    private static async Task InvalidWelcome()
    {
        var builder = WebApplication.CreateSlimBuilder(); builder.Logging.ClearProviders();
        builder.WebHost.ConfigureKestrel(o => o.Listen(IPAddress.Loopback, 0));
        await using var fake = builder.Build();
        int attempt = 0;
        fake.MapPost("/join", () => ++attempt == 1
            ? Results.Json(new { playerId = Guid.Empty, token = "bad", protocolVersion = 2 })
            : Results.Text("private-response-body-not-for-ui", "text/html"));
        await fake.StartAsync();
        var address = new Uri(fake.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single());
        await ExpectCode(() => LobbyClient.JoinAsync(address, "test"), "incompatible_server");
        await ExpectCode(() => LobbyClient.JoinAsync(address, "test"), "incompatible_server");
    }
}
