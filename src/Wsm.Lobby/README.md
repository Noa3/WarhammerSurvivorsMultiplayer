# Wsm.Lobby

Standalone .NET 10 HTTP/JSON lobby service for player selections, readiness and session rules. Reference `Wsm.Lobby.csproj` from the desktop application; `Microsoft.AspNetCore.App` is a framework reference. Gameplay transport and a game adapter must be supplied separately.

## API

```csharp
using System.Net;
using Wsm.Lobby;

await using var host = await LobbyServer.StartAsync(
    IPAddress.Loopback, 7777, "Host", new LobbyRules(
        BotCount: 1, BotDifficulty: BotDifficulty.Normal,
        DefeatRule: DefeatRule.AllPlayersDefeated));
Uri address = host.Address;
LobbySnapshot hostView = host.Snapshot;
host.Configure(new LobbyRules(BotCount: 0)); // host process only
host.SetHostLoadout("game-hero-id", new[] { "game-weapon-id" });
host.SetHostReady(true);
StartResult start = host.TryStart(); // default: refuses; adapter unavailable

await using var guest = await LobbyClient.JoinAsync(address, "Guest");
Guid id = guest.PlayerId;
await guest.SetLoadoutAsync("game-hero-id", new[] { "game-weapon-id" });
await guest.SetReadyAsync(true);
LobbySnapshot guestView = await guest.GetStateAsync();
await guest.HeartbeatAsync();
await guest.LeaveAsync(); // also attempted by DisposeAsync
```

All client async operations accept an optional final `CancellationToken`. `StartAsync(IPAddress bindAddress, int port, string hostName, LobbyRules rules, IGameLaunchCoordinator? coordinator = null, CancellationToken cancellationToken = default)` accepts port 0 for an OS-assigned port. Host-only synchronous methods are `Configure(LobbyRules)`, `SetHostLoadout(string, IEnumerable<string>)`, `SetHostReady(bool)`, `TryStart()`. `Snapshot` is a copied, versioned view. `LobbyClient` throws `HttpRequestException` with `StatusCode` for HTTP failures; host validation throws `ArgumentException` or `InvalidOperationException`.

Use loopback unless LAN access is explicitly wanted. Bind the specific LAN address or `IPAddress.Any`; for wildcard binding `Address` is a loopback URI for the local host, **not** an advertised LAN address. Share the actual reachable host IP and assigned port separately. This protocol is **unencrypted HTTP with bearer tokens and no password**; only use on a trusted LAN/VPN. It is not safe on the public internet or on untrusted networks. No firewall changes are made.

There are at most four total slots, including host and bots. Rules cannot evict humans; a capacity-conflicting change fails without dropping anyone. Joining, leaving, expiry and changed rules clear all human readiness; changing one's loadout clears that player's readiness. A ready human needs an explicit hero and 1–8 weapon IDs, each at most 64 characters. IDs are opaque, **unvalidated user input**: the game catalog and actual selection validation are unavailable. Bot entries are lobby configuration only. Slot `PlayerId` for bots is `Guid.Empty` and is not an ownership token.

Clients must call `HeartbeatAsync` (or another authenticated operation) periodically, e.g. every 5 seconds. Guests inactive for more than 15 seconds are removed and readiness is reset. There is no token-based reconnection: after expiry join again and reselect. Poll `GetStateAsync` for versioned updates. A leave request or disposal removes the guest; an expired session's leave is treated as already complete. Network failures during disposal can still throw. Server disposal stops the bounded cleanup task and Kestrel. Do not assume it disconnects or controls an actual game.

## Start and pause boundaries

All humans must be ready with loadouts before `TryStart`. With no `IGameLaunchCoordinator`, start refuses without marking the lobby started. A real adapter can be injected via `StartAsync`; its synchronous `TryLaunch(LobbySnapshot)` must honestly return `new StartResult(true, ...)` only after a real successful start. External launch work runs outside the state lock: authenticated heartbeats and reads remain available while other lobby mutations, including leave, are refused. Call a real potentially slow coordinator off the UI thread and bound its work. Lobby expiry is suspended during launch and after a successful start; the future game adapter must own in-run disconnect handling. Exceptions propagate, and a refused/failed launch grants guests a fresh lobby heartbeat interval. No such adapter exists here, and tests use only a fixture. A successful coordinator marks the control-plane snapshot started; it does not implement game networking, disconnect recovery, cancellation or rollback.

`LobbyRules.PausePolicy` exposes `GlobalUpgradesPersonalMenusNonPausingHostConfirmedManualPause`: intended global upgrade decisions, personal menus nonpausing, and host-confirmed manual pause. **It describes policy only**, not an executed pause or upgrade mechanism. Game integration must implement and verify those semantics and actual disconnect/gameplay behavior.

## Verification

`dotnet run --project tests/Wsm.Lobby.Tests/Wsm.Lobby.Tests.csproj` runs the executable integration tests against loopback Kestrel.

The client bounds buffered HTTP responses, including errors, to 32 KiB. Readiness requests require an explicit boolean. Regression tests include an oversized valid response from a controlled server, malformed readiness preserving the current state, and heartbeats during a fixture launch held beyond the normal expiry interval. These are control-plane tests, not a real game launch.
