# Warhammer Survivors Multiplayer

A Windows co-op project for **Warhammer Survivors**, with an IP lobby and game-inspection tools.

> **In development:** You can create and join lobbies, choose loadouts and configure session rules. Multiplayer gameplay is not connected yet, so the application cannot start a co-op run.

## Get started

1. Download this repository using **Code > Download ZIP**, or clone it.
2. Extract the complete repository and keep the included files together.
3. Open `distribution/win-x64/lobby/Wsm.App.exe` on Windows x64.

The package includes the .NET runtime. See the [Windows package guide](distribution/README.md) for its contents and checksums.

### Host or join a lobby

The interface is in German. Create a lobby with your player name and a port; the default port is **7780**. The default loopback binding lets you test two application windows on the same computer using `127.0.0.1`.

For a trusted LAN or VPN, select the LAN binding and share your reachable IPv4 address and port. Guests select **Beitreten** and enter those details.

- A lobby has four slots, including the host and bots.
- Each human enters their own hero and weapon IDs, submits the selection and selects **Bereit**.
- The host can configure bot slots, bot difficulty and the defeat rule.
- Failed connections show the attempted address, a reason and a suggested next step.

Hero and weapon IDs are currently entered manually and are not checked against the game's unlocks. Bot and defeat settings are lobby configuration; they do not control a running game yet. **Start anfragen** reports that game integration is unavailable.

The lobby uses unencrypted HTTP and has no password. Use a trusted LAN or secured VPN, not a public internet port. Cross-machine connectivity has not yet been tested. For connection problems, see [connection troubleshooting](docs/CONNECTION_HELP.md).

## Planned gameplay

The intended co-op mode uses a shared world with synchronized enemies and health, while each player keeps their own progression and upgrade choices. The host will be able to fill vacant slots with bots and choose between spectating after death or ending the run on the first death.

Game synchronization, in-game bots, coordinated pauses, configurable XP/pickup sharing and progress-preserving reconnect are still being developed.

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
