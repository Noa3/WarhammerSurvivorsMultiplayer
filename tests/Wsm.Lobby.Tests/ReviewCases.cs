using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Wsm.Lobby;

static class ReviewCases
{
    public static async Task Run()
    {
        var failures = new List<Exception>();
        await Case("oversized host responses are bounded", async () =>
        {
            var builder = WebApplication.CreateSlimBuilder();
            builder.Logging.ClearProviders();
            builder.WebHost.ConfigureKestrel(o => o.Listen(IPAddress.Loopback, 0));
            await using var hostile = builder.Build();
            hostile.MapPost("/join", () => Results.Json(new { playerId = Guid.NewGuid(), token = new string('A', 64), protocolVersion = LobbyProtocol.Version, padding = new string('x', 100000) }));
            await hostile.StartAsync();
            var uri = new Uri(hostile.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single());
            bool rejected = false;
            try { await LobbyClient.JoinAsync(uri, "fixture"); }
            catch (HttpRequestException) { rejected = true; }
            if (!rejected) throw new Exception("Oversized valid join response was accepted.");
        });
        await Case("missing readiness is rejected without changing readiness", async () =>
        {
            await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "host", new());
            using var raw = new HttpClient { BaseAddress = server.Address };
            using var joined = await raw.PostAsJsonAsync("/join", new { name = "guest", protocolVersion = LobbyProtocol.Version });
            using var body = JsonDocument.Parse(await joined.Content.ReadAsStringAsync());
            raw.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", body.RootElement.GetProperty("token").GetString());
            using var loadout = await raw.PostAsJsonAsync("/loadout", new { heroId = "fixture-hero", weaponIds = new[] { "fixture-weapon" } });
            loadout.EnsureSuccessStatusCode();
            using var ready = await raw.PostAsJsonAsync("/ready", new { ready = true }); ready.EnsureSuccessStatusCode();
            foreach (var invalid in new object[] { new { }, new { redy = false }, new { ready = (bool?)null } })
            {
                using var response = await raw.PostAsJsonAsync("/ready", invalid);
                if (response.StatusCode != HttpStatusCode.BadRequest || !server.Snapshot.Slots.Single(s => s.Kind == SlotKind.Player).Ready)
                    throw new Exception("Malformed readiness changed state or was accepted.");
            }
        });
        await Case("launch does not block heartbeats or expire active guests", async () =>
        {
            var coordinator = new BlockingCoordinator();
            await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "host", new(), coordinator);
            await using var guest = await LobbyClient.JoinAsync(server.Address, "guest");
            server.SetHostLoadout("host-hero", ["host-weapon"]); await guest.SetLoadoutAsync("guest-hero", ["guest-weapon"]);
            server.SetHostReady(true); await guest.SetReadyAsync(true);
            var start = Task.Run(server.TryStart);
            try
            {
                if (!coordinator.Entered.Wait(3000)) throw new Exception("Coordinator not called.");
                await guest.HeartbeatAsync().WaitAsync(TimeSpan.FromSeconds(1));
                for (var i = 0; i < 6; i++) { await Task.Delay(2800); await guest.HeartbeatAsync().WaitAsync(TimeSpan.FromSeconds(1)); }
            }
            finally { coordinator.Release.Set(); await start; }
            if (!server.Snapshot.Started || !server.Snapshot.Slots.Any(s => s.PlayerId == guest.PlayerId)) throw new Exception("Active guest disappeared during launch.");
        });
        if (failures.Count != 0) throw new AggregateException(failures);
        async Task Case(string name, Func<Task> body)
        {
            try { await body(); Console.WriteLine("PASS " + name); }
            catch (Exception ex) { failures.Add(ex); Console.WriteLine("FAIL " + name + ": " + ex.Message); }
        }
    }
    private sealed class BlockingCoordinator : IGameLaunchCoordinator
    {
        public ManualResetEventSlim Entered { get; } = new();
        public ManualResetEventSlim Release { get; } = new();
        public StartResult TryLaunch(LobbySnapshot snapshot) { Entered.Set(); if (!Release.Wait(25000)) throw new TimeoutException("Fixture release missing."); return new(true, "fixture only"); }
    }
}
