using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Wsm.Lobby;

public sealed class LobbyServer : IAsyncDisposable
{
    private sealed class Person(Guid id, string name, string? token, SlotKind kind)
    {
        public Guid Id = id; public string Name = name; public string? Token = token; public SlotKind Kind = kind;
        public string? Hero; public string[] Weapons = []; public bool Ready; public bool Leaving; public DateTimeOffset Seen = DateTimeOffset.UtcNow;
    }
    private readonly object gate = new();
    private readonly List<Person> people = [];
    private readonly IGameLaunchCoordinator? coordinator;
    private readonly WebApplication app;
    private readonly CancellationTokenSource stop = new();
    private Task? sweeper;
    private LobbyRules rules;
    private long version = 1;
    private bool started;
    private bool launching;
    private bool disposed;
    public Uri Address { get; private set; } = null!;
    public LobbySnapshot Snapshot { get { lock (gate) { Prune(); return Copy(); } } }
    private LobbyServer(WebApplication app, string name, LobbyRules rules, IGameLaunchCoordinator? coordinator)
    {
        this.app = app; this.rules = rules; this.coordinator = coordinator;
        people.Add(new(Guid.NewGuid(), name, null, SlotKind.Host));
    }
    public static async Task<LobbyServer> StartAsync(IPAddress bindAddress, int port, string hostName, LobbyRules rules, IGameLaunchCoordinator? coordinator = null, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(bindAddress); ValidateText(hostName, nameof(hostName)); ValidateRules(rules);
        if (port is < 0 or > 65535) throw new ArgumentOutOfRangeException(nameof(port));
        var builder = WebApplication.CreateSlimBuilder(new WebApplicationOptions { ApplicationName = typeof(LobbyServer).Assembly.FullName });
        builder.Logging.ClearProviders();
        builder.WebHost.ConfigureKestrel(options => { options.Limits.MaxRequestBodySize = 16384; options.Listen(bindAddress, port); });
        var app = builder.Build();
        var server = new LobbyServer(app, hostName, rules, coordinator);
        app.Run(server.Handle);
        try {
            await app.StartAsync(cancellationToken);
            var address = app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single();
            var actual = new Uri(address);
            var clientHost = bindAddress.Equals(IPAddress.Any) ? IPAddress.Loopback : bindAddress.Equals(IPAddress.IPv6Any) ? IPAddress.IPv6Loopback : bindAddress;
            server.Address = new UriBuilder("http", clientHost.ToString(), actual.Port).Uri;
            server.sweeper = server.SweepAsync();
            return server;
        } catch { await app.DisposeAsync(); throw; }
    }
    public void Configure(LobbyRules value)
    {
        ValidateRules(value);
        lock (gate) { EnsureMutable(); Prune(); if (people.Count + value.BotCount > 4) throw new InvalidOperationException("Not enough slots; no players were removed.");
            if (rules == value) return; rules = value; foreach (var p in people) p.Ready = false; version++; }
    }
    public void SetHostLoadout(string heroId, IEnumerable<string> weaponIds) { var weapons = ValidateLoadout(heroId, weaponIds); lock (gate) { EnsureMutable(); SetLoadout(people[0], heroId, weapons); } }
    public void SetHostReady(bool ready) { lock (gate) { EnsureMutable(); SetReady(people[0], ready); } }
    public StartResult TryStart()
    {
        LobbySnapshot launchSnapshot;
        lock (gate) {
            EnsureMutable(); Prune();
            if (people.Any(p => !p.Ready || p.Hero is null || p.Weapons.Length == 0)) return new(false, "All human players must choose a hero and at least one weapon and be ready.");
            if (coordinator is null) return new(false, "Gameplay adapter unavailable; lobby cannot start the game.");
            launchSnapshot = Copy();
            launching = true;
        }
        try {
            // External launch work must never block network heartbeats behind the state lock.
            var result = coordinator.TryLaunch(launchSnapshot) ?? throw new InvalidOperationException("Coordinator returned no result.");
            lock (gate) {
                ObjectDisposedException.ThrowIf(disposed, this);
                if (result.Started) { started = true; version++; }
            }
            return result;
        } finally {
            lock (gate) {
                launching = false;
                if (people.RemoveAll(p => p.Leaving) > 0) { foreach (var p in people) p.Ready = false; version++; }
                // Lobby expiry is suspended during launch; grant a fresh recovery window on refusal.
                foreach (var p in people) p.Seen = DateTimeOffset.UtcNow;
            }
        }
    }
    private void EnsureMutable() { ObjectDisposedException.ThrowIf(disposed, this); if (started || launching) throw new InvalidOperationException("Lobby already started or launch is in progress."); }
    private static void ValidateText(string? value, string name) { if (string.IsNullOrWhiteSpace(value) || value.Length > 64 || value.Any(char.IsControl)) throw new ArgumentException("Must be 1–64 non-control characters.", name); }
    private static void ValidateRules(LobbyRules value) { ArgumentNullException.ThrowIfNull(value); value.Validate(); }
    private static string[] ValidateLoadout(string? hero, IEnumerable<string>? weapons)
    {
        ValidateText(hero, "heroId"); if (weapons is null) throw new ArgumentException("Weapons required.");
        var result = weapons.Take(9).ToArray(); if (result.Length is < 1 or > 8) throw new ArgumentException("Select 1–8 weapons.");
        foreach (var w in result) ValidateText(w, "weaponId"); return result;
    }
    private void SetLoadout(Person p, string hero, string[] weapons) { if (p.Hero == hero && p.Weapons.SequenceEqual(weapons)) return; p.Hero = hero; p.Weapons = weapons; p.Ready = false; version++; }
    private void SetReady(Person p, bool ready) { if (ready && (p.Hero is null || p.Weapons.Length == 0)) throw new InvalidOperationException("Select a hero and weapon first."); if (p.Ready != ready) { p.Ready = ready; version++; } }
    private LobbySnapshot Copy()
    {
        var slots = people.Select(p => new LobbySlot(p.Id, p.Name, p.Kind, p.Hero, Array.AsReadOnly((string[])p.Weapons.Clone()), p.Ready)).ToList();
        for (int i = 0; i < rules.BotCount; i++) slots.Add(new LobbySlot(Guid.Empty, $"Bot {i + 1}", SlotKind.Bot, null, Array.Empty<string>(), true));
        return new(version, rules, slots.AsReadOnly(), started);
    }
    private void Prune() { if (started || launching) return; if (people.RemoveAll(p => p.Kind == SlotKind.Player && DateTimeOffset.UtcNow - p.Seen > TimeSpan.FromSeconds(15)) > 0) { foreach (var p in people) p.Ready = false; version++; } }
    private async Task SweepAsync() { try { using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1)); while (await timer.WaitForNextTickAsync(stop.Token)) lock (gate) Prune(); } catch (OperationCanceledException) when (stop.IsCancellationRequested) { } }
    private async Task Handle(HttpContext ctx)
    {
        try {
            if (ctx.Request.Method == "POST" && ctx.Request.Path == "/join") {
                var req = await ctx.Request.ReadFromJsonAsync<JoinRequest>() ?? throw new ArgumentException("Missing body."); ValidateText(req.Name, "name");
                if (req.ProtocolVersion != LobbyProtocol.Version) throw new LobbyRejection(426, "protocol_mismatch", $"Lobby protocol {LobbyProtocol.Version} required.");
                JoinResponse response;
                lock (gate) { ObjectDisposedException.ThrowIf(disposed, this); if (started || launching) throw new LobbyRejection(409, "lobby_unavailable", "The lobby is already starting or started."); Prune(); if (people.Count + rules.BotCount >= 4) throw new LobbyRejection(409, "lobby_full", "Lobby is full; human and bot slots are occupied.");
                    var token = Convert.ToHexString(RandomNumberGenerator.GetBytes(32)); var p = new Person(Guid.NewGuid(), req.Name, token, SlotKind.Player); people.Add(p); foreach (var person in people) person.Ready = false; version++; response = new(p.Id, token, LobbyProtocol.Version); }
                await ctx.Response.WriteAsJsonAsync(response); return;
            }
            var auth = ctx.Request.Headers.Authorization.ToString();
            string tokenValue = auth.StartsWith("Bearer ", StringComparison.Ordinal) ? auth[7..] : "";
            // Authenticate before parsing any mutation; IDs in request bodies have no authority.
            if (tokenValue.Length != 64) throw new LobbyRejection(401, "session_expired", "Lobby session is absent or no longer valid.");
            object? request = null;
            if (ctx.Request.Method == "POST" && ctx.Request.Path == "/loadout") request = await ctx.Request.ReadFromJsonAsync<LoadoutRequest>() ?? throw new ArgumentException("Missing body.");
            else if (ctx.Request.Method == "POST" && ctx.Request.Path == "/ready") request = await ctx.Request.ReadFromJsonAsync<ReadyRequest>() ?? throw new ArgumentException("Missing body.");
            LobbySnapshot? snapshot = null;
            lock (gate) {
                Prune(); var p = people.FirstOrDefault(x => x.Token is not null && CryptographicOperations.FixedTimeEquals(System.Text.Encoding.ASCII.GetBytes(x.Token), System.Text.Encoding.ASCII.GetBytes(tokenValue)));
                if (p is null || p.Leaving) throw new LobbyRejection(401, "session_expired", "Lobby session is absent or no longer valid.");
                p.Seen = DateTimeOffset.UtcNow;
                if (ctx.Request.Method == "GET" && ctx.Request.Path == "/state") snapshot = Copy();
                else if (ctx.Request.Method == "POST" && ctx.Request.Path == "/heartbeat") { }
                else if (ctx.Request.Method == "POST" && ctx.Request.Path == "/leave") {
                    if (launching) p.Leaving = true;
                    else { people.Remove(p); foreach (var person in people) person.Ready = false; version++; }
                }
                else if (request is LoadoutRequest loadout) { EnsureMutable(); SetLoadout(p, loadout.HeroId, ValidateLoadout(loadout.HeroId, loadout.WeaponIds)); }
                else if (request is ReadyRequest ready) { EnsureMutable(); SetReady(p, ready.Ready ?? throw new ArgumentException("An explicit ready boolean is required.")); }
                else { ctx.Response.StatusCode = 404; return; }
            }
            if (snapshot is not null) await ctx.Response.WriteAsJsonAsync(snapshot); else ctx.Response.StatusCode = 204;
        } catch (LobbyRejection ex) {
            ctx.Response.StatusCode = ex.Status; await ctx.Response.WriteAsJsonAsync(new ErrorResponse(ex.Message, ex.Code));
        } catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or JsonException or BadHttpRequestException) {
            ctx.Response.StatusCode = ex is InvalidOperationException ? 409 : 400; await ctx.Response.WriteAsJsonAsync(new ErrorResponse(ex.Message, ex is InvalidOperationException ? "state_conflict" : "invalid_request"));
        }
    }
    public async ValueTask DisposeAsync() { lock (gate) { if (disposed) return; disposed = true; } stop.Cancel(); if (sweeper is not null) await sweeper; await app.StopAsync(); await app.DisposeAsync(); stop.Dispose(); }
}
