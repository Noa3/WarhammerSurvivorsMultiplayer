# Warhammer Survivors Multiplayer

A Windows co-op project for **Warhammer Survivors**, with an IP lobby and game-inspection tools.

> **In development:** You can create and join lobbies, choose loadouts and configure session rules. Multiplayer gameplay is not connected yet, so the application cannot start a co-op run.

## Get started

1. Open [GitHub Releases](https://github.com/Noa3/WarhammerSurvivorsMultiplayer/releases) and download **WSM-portable-win-x64.zip** from the tools preview.
2. Extract its complete `WSM` folder into a new location. Keep the accompanying files together.
3. Open `WSM/lobby/Wsm.App.exe` on Windows x64.

The package includes the .NET runtime; Unity and development tools are not needed to use it. The repository ZIP also includes the application under `distribution/win-x64/lobby/Wsm.App.exe`. See the [Windows package guide](distribution/README.md) for contents and checksums, and the [distribution guide](docs/DISTRIBUTION.md) for the experimental Vortex archive and its restrictions.

### Host or join a lobby

The interface is in German. Create a lobby with your player name and a port; the default port is **7780**. The default loopback binding lets you test two application windows on the same computer using `127.0.0.1`.

For a trusted LAN or VPN, select the LAN binding and share your reachable IPv4 address and port. Guests select **Beitreten** and enter those details.

- A lobby has four slots, including the host and bots.
- Each human enters their own hero and weapon IDs, submits the selection and selects **Bereit**.
- The host can configure bot slots, bot difficulty, the defeat rule, XP sharing, XP-drop layout and pickup sharing.
- **Bestätigte Regeln** shows the applied rules. Editing a host field creates a draft; **Host-Regeln übernehmen** applies it and resets readiness.
- The lobby maintains its connection in the background, independently of roster refreshes.
- Failed connections show the attempted address, a reason and a suggested next step.

Hero and weapon IDs are currently entered manually and are not checked against the game's unlocks. Session rules are available in the lobby, but do not control a running game yet. **Start anfragen** reports that game integration is unavailable. See [session rules](docs/SESSION_RULES.md) for the sharing options and their current limits. Use the same application revision on every computer; older protocol versions cannot join.

The lobby uses unencrypted HTTP and has no password. Use a trusted LAN or secured VPN, not a public internet port. Cross-machine connectivity has not yet been tested. For connection problems, see [connection troubleshooting](docs/CONNECTION_HELP.md).

### Optional separate game launch

**Spiel separat starten (experimentell)** prepares a separate copy of your installed game and starts it windowed while leaving the lobby accessible. The confirmation warns that **saves and Steam Cloud are not isolated**. This is not a co-op start or an in-game menu. See [launch precautions and cache storage](docs/INGAME_MENUS.md#optional-single-player-launch).

## Planned gameplay

The intended co-op mode uses a shared world with synchronized enemies and health, while each player keeps their own progression and upgrade choices. The host will be able to fill vacant slots with bots and choose between spectating after death or ending the run on the first death.

Game synchronization, in-game bots, coordinated pauses, applying XP/pickup rules to game objects and progress-preserving reconnect are still being developed. The [in-game menu guide](docs/INGAME_MENUS.md) explains the intended campaign-to-lobby flow and the boundary between menu browsing, network liveness and gameplay pauses.

## Development

Build from source with the .NET 10 SDK, PowerShell 7 and the Windows C++ build tools described in the [developer guide](docs/DEVELOPMENT.md). From the repository root:

```powershell
pwsh -File tools/verify.ps1
```

The script builds the projects and runs native, network, launcher and Windows Forms tests. The GUI tests require an interactive Windows desktop. Automated checks cover the tools and libraries; gameplay needs separate testing once the game adapter exists.

Component documentation:

- [Windows lobby application](src/Wsm.App/README.md)
- [Lobby API](src/Wsm.Lobby/README.md)
- [Session and progression API](src/Wsm.Core/README.md)
- [Native inspection bridge](native/README.md)

The game-inspection tools have been tested with game **2.1.6**, Unity **6000.0.74f1** and IL2CPP metadata **31** (Steam app **3669620**). Other game versions have not been tested. See the developer guide for [inspection and runtime preparation](docs/DEVELOPMENT.md#inspect-and-prepare) and [game-probe save precautions](docs/DEVELOPMENT.md#game-probes-and-save-data).

## Help and contributions

Report bugs or suggest changes through [GitHub Issues](https://github.com/Noa3/WarhammerSurvivorsMultiplayer/issues). Include the application revision, Windows and game versions, reproduction steps and the error message. Remove personal information and session credentials from any attached logs.

For code changes, run the verification script and describe which behavior you tested. Keep game files and saves out of contributions.

This is an unofficial project, unaffiliated with the game's developers or rights holders. Game diagnostics require your own installed copy. Third-party runtime license notices are included in the [Windows package](distribution/win-x64/licenses/).
