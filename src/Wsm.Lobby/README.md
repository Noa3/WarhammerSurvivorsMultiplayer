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
await guest.HeartbeatAsync(); // optional immediate heartbeat; background keepalive is automatic
await guest.LeaveAsync(); // also attempted by DisposeAsync
```

All client async operations accept an optional final `CancellationToken`. `StartAsync(IPAddress bindAddress, int port, string hostName, LobbyRules rules, IGameLaunchCoordinator? coordinator = null, CancellationToken cancellationToken = default)` accepts port 0 for an OS-assigned port. Host-only synchronous methods are `Configure(LobbyRules)`, `SetHostLoadout(string, IEnumerable<string>)`, `SetHostReady(bool)`, `TryStart()`. `Snapshot` is a copied, versioned view. `LobbyClient` throws `HttpRequestException` with `StatusCode` for HTTP failures; host validation throws `ArgumentException` or `InvalidOperationException`.

Use loopback unless LAN access is explicitly wanted. Bind the specific LAN address or `IPAddress.Any`; for wildcard binding `Address` is a loopback URI for the local host, **not** an advertised LAN address. Share the actual reachable host IP and assigned port separately. This protocol is **unencrypted HTTP with bearer tokens and no password**; only use on a trusted LAN/VPN. It is not safe on the public internet or on untrusted networks. No firewall changes are made.

There are at most four total slots, including host and bots. Rules cannot evict humans; a capacity-conflicting change fails without dropping anyone. Joining, leaving, expiry and changed rules clear all human readiness; changing one's loadout clears that player's readiness. A ready human needs an explicit hero and 1–8 weapon IDs, each at most 64 characters. IDs are opaque, **unvalidated user input**: the game catalog and actual selection validation are unavailable. Bot entries are lobby configuration only. Slot `PlayerId` for bots is `Guid.Empty` and is not an ownership token.

`LobbyClient` automatically sends authenticated heartbeats every three seconds on a background task, independently of UI polling, gameplay updates and the game's pause clock. Each heartbeat has a three-second request deadline. `LastHeartbeatError` exposes the last observed failure and clears after recovery; an expired/incompatible session stops the loop rather than silently rejoining. Guests from custom/raw clients that send nothing for more than 15 seconds are still removed and readiness is reset. There is no token-based reconnection: after expiry join again and reselect. Poll `GetStateAsync` for versioned updates. `LeaveAsync` keeps the heartbeat running until leave succeeds or the server reports an expired session; on other failures it remains available for a retry. A timeout or cancellation does not prove whether the server applied the request: a subsequent 401 can mean it already did. Disposal makes a bounded leave attempt, then stops the heartbeat and closes HTTP even if cleanup fails; disposal is idempotent and network failures can still throw. Server disposal stops the bounded cleanup task and Kestrel. Do not assume it disconnects or controls an actual game.

## Start and pause boundaries

All humans must be ready with loadouts before `TryStart`. With no `IGameLaunchCoordinator`, start refuses without marking the lobby started. A real adapter can be injected via `StartAsync`; its synchronous `TryLaunch(LobbySnapshot)` must honestly return `new StartResult(true, ...)` only after a real successful start. External launch work runs outside the state lock: authenticated heartbeats and reads remain available while ordinary lobby mutations are refused. A leave during launch is acknowledged and queued; the token is immediately invalidated and the slot is removed when the coordinator returns or throws, with readiness reset and a version bump. The coordinator's copied launch snapshot remains unchanged. Call a real potentially slow coordinator off the UI thread and bound its work. Lobby expiry is suspended during launch and after a successful start; the future game adapter must handle departures during launch and in-run disconnects. Exceptions propagate, and a refused/failed launch grants remaining guests a fresh lobby heartbeat interval. No such adapter exists here, and tests use only a fixture. A successful coordinator marks the control-plane snapshot started even if the remaining roster has changed; this cleanup does not imply that a game was aborted, safely rolled back, or informed of the departure. It does not implement game networking, disconnect recovery, cancellation or rollback.

`LobbyRules.PausePolicy` exposes `GlobalUpgradesPersonalMenusNonPausingHostConfirmedManualPause`: intended global upgrade decisions, personal menus nonpausing, and host-confirmed manual pause. **It describes policy only**, not an executed pause or upgrade mechanism. Game integration must implement and verify those semantics and actual disconnect/gameplay behavior.

## Verification

`dotnet run --project tests/Wsm.Lobby.Tests/Wsm.Lobby.Tests.csproj` runs the executable integration tests against loopback Kestrel.

The client bounds buffered HTTP responses, including errors, to 32 KiB. Readiness requests require an explicit boolean. Regression tests include an oversized valid response from a controlled server, malformed readiness preserving the current state, and heartbeats during a fixture launch held beyond the normal expiry interval. These are control-plane tests, not a real game launch.
