using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using Wsm.Lobby;

static class NetworkCases
{
    static void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
    static async Task ExpectStatus(Func<Task> action, HttpStatusCode status) { try { await action(); throw new Exception("Expected HTTP " + status); } catch (HttpRequestException ex) when (ex.StatusCode == status) { } }
    static async Task Case(string name, Func<Task> action) { await action(); Console.WriteLine("PASS " + name); }
    public static async Task Run()
    {
        await Case("independent selections, readiness and start guard", async () => {
            await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "Host", new LobbyRules());
            await using var a = await LobbyClient.JoinAsync(server.Address, "Alice");
            await using var b = await LobbyClient.JoinAsync(server.Address, "Bob");
            var v = server.Snapshot.Version;
            await a.SetLoadoutAsync("hero-a", new[] { "axe" }); await b.SetLoadoutAsync("hero-b", new[] { "bow" });
            server.SetHostLoadout("host-id", new[] { "staff" });
            await a.SetReadyAsync(true); await b.SetReadyAsync(true); server.SetHostReady(true);
            var s = await a.GetStateAsync();
            Assert(s.Version > v && s.Slots.Count == 3 && s.Slots.Single(x => x.PlayerId == a.PlayerId).HeroId == "hero-a" && s.Slots.Single(x => x.PlayerId == b.PlayerId).HeroId == "hero-b", "independent state");
            var refusal = server.TryStart(); Assert(!refusal.Started && refusal.Reason.Contains("adapter", StringComparison.OrdinalIgnoreCase) && !server.Snapshot.Started, "default start must refuse");
            server.Configure(new LobbyRules(1, BotDifficulty.Hard, DefeatRule.AnyPlayerDefeated));
            s = server.Snapshot; Assert(s.Slots.Count == 4 && s.Slots.Count(x => x.Kind == SlotKind.Bot) == 1 && s.Slots.Where(x => x.Kind != SlotKind.Bot).All(x => !x.Ready), "rules reset readiness");
            try { server.Configure(new LobbyRules(2)); throw new Exception("expected capacity rejection"); } catch (InvalidOperationException) { }
            Assert(server.Snapshot.Slots.Count == 4 && server.Snapshot.Slots.Any(x => x.PlayerId == b.PlayerId), "configure must not drop players");
            await ExpectStatus(() => LobbyClient.JoinAsync(server.Address, "extra"), HttpStatusCode.Conflict);
        });
        await Case("token ownership and no remote administration", async () => {
            await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "Host", new LobbyRules());
            using var raw = new HttpClient { BaseAddress = server.Address };
            using var joinA = await raw.PostAsJsonAsync("/join", new { name = "A", protocolVersion = LobbyProtocol.Version }); joinA.EnsureSuccessStatusCode();
            using var ja = JsonDocument.Parse(await joinA.Content.ReadAsStringAsync()); var token = ja.RootElement.GetProperty("token").GetString()!; var idA = ja.RootElement.GetProperty("playerId").GetGuid();
            await using var b = await LobbyClient.JoinAsync(server.Address, "B"); await b.SetLoadoutAsync("b-hero", new[] { "b-weapon" });
            raw.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token);
            using var mutate = await raw.PostAsJsonAsync("/loadout", new { playerId = b.PlayerId, heroId = "a-hero", weaponIds = new[] { "a-weapon" } }); mutate.EnsureSuccessStatusCode();
            var snap = server.Snapshot; Assert(snap.Slots.Single(x => x.PlayerId == idA).HeroId == "a-hero" && snap.Slots.Single(x => x.PlayerId == b.PlayerId).HeroId == "b-hero", "ownership");
            using var state = await raw.GetAsync("/state"); var text = await state.Content.ReadAsStringAsync(); Assert(!text.Contains(token), "token leak");
            using var admin = await raw.PostAsJsonAsync("/start", new { }); Assert(admin.StatusCode == HttpStatusCode.NotFound && !server.Snapshot.Started, "remote start");
            raw.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", new string('0', 64));
            using var denied = await raw.GetAsync("/state"); Assert(denied.StatusCode == HttpStatusCode.Unauthorized, "wrong token");
        });
        await Case("bounded invalid requests", async () => {
            await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "Host", new LobbyRules());
            await using var c = await LobbyClient.JoinAsync(server.Address, "A");
            await ExpectStatus(() => c.SetReadyAsync(true), HttpStatusCode.Conflict);
            await ExpectStatus(() => c.SetLoadoutAsync("x", Enumerable.Repeat("x", 9)), HttpStatusCode.BadRequest);
            using var raw = new HttpClient { BaseAddress = server.Address };
            using var huge = await raw.PostAsync("/join", new StringContent("{\"name\":\"" + new string('x', 17000) + "\"}", Encoding.UTF8, "application/json"));
            Assert(!huge.IsSuccessStatusCode && server.Snapshot.Slots.Count == 2, "oversized join");
        });
        await Case("fixture coordinator starts only when ready", async () => {
            var fixture = new FixtureCoordinator();
            await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "Host", new LobbyRules(), fixture);
            Assert(!server.TryStart().Started && fixture.Calls == 0, "unready guard");
            server.SetHostLoadout("opaque", new[] { "weapon" }); server.SetHostReady(true);
            Assert(server.TryStart().Started && fixture.Calls == 1 && server.Snapshot.Started, "fixture invoked");
        });
        await Case("heartbeat expiry removes only stale guest", async () => {
            await using var server = await LobbyServer.StartAsync(IPAddress.Loopback, 0, "Host", new LobbyRules());
            var stale = await LobbyClient.JoinAsync(server.Address, "stale");
            await using var active = await LobbyClient.JoinAsync(server.Address, "active");
            server.SetHostLoadout("hero", new[] { "weapon" }); server.SetHostReady(true);
            for (var i = 0; i < 6; i++) { await Task.Delay(3000); await active.HeartbeatAsync(); }
            var snapshot = server.Snapshot;
            Assert(!snapshot.Slots.Any(x => x.PlayerId == stale.PlayerId) && snapshot.Slots.Any(x => x.PlayerId == active.PlayerId) && !snapshot.Slots.Single(x => x.Kind == SlotKind.Host).Ready, "expiry and ready reset");
            await ExpectStatus(() => stale.GetStateAsync(), HttpStatusCode.Unauthorized);
            try { await stale.DisposeAsync(); } catch (HttpRequestException ex) when (ex.StatusCode == HttpStatusCode.Unauthorized) { }
        });
    }
    sealed class FixtureCoordinator : IGameLaunchCoordinator
    {
        public int Calls { get; private set; }
        public StartResult TryLaunch(LobbySnapshot snapshot) { Calls++; return new(true, "fixture only"); }
    }
}
