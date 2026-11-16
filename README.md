# Warhammer Survivors Multiplayer

An experimental Windows co-op project for **Warhammer Survivors**, with hosting and IP connections directly inside the game.

> **Connection preview, not playable co-op yet.** The in-game panel can host, join and leave a real network session. Shared enemies, remote player control, personal upgrades and bots are not connected to gameplay yet.

## Try the in-game panel

1. Download the complete repository ZIP or clone this repository.
2. Open `distribution/win-x64/Start-WSM.cmd` and enter your installed Warhammer Survivors folder.
3. Read the save warning and confirm the launch. The launcher prepares a separate installation copy and starts the game.
4. Open **Co-op [F6]** inside the game. Enter an IPv4 address and port, then choose **Host**, **Join** or **Leave**.

Keep the complete Windows package together. The packaged tools include their .NET runtime; Unity and development tools are not required to use them. The native bridge also requires the free [Microsoft Visual C++ x64 runtime](https://aka.ms/vc14/vc_redist.x64.exe), which may already be installed with the game. Your own installed game and its normal Steam prerequisites are required. Older tools-preview assets on [GitHub Releases](https://github.com/Noa3/WarhammerSurvivorsMultiplayer/releases) do not include this newer in-game interface.

**The installation copy does not isolate saves or Steam Cloud.** The game still uses its normal profile and can update it. Back up valuable saves before launching. The launcher does not restore saves automatically.

The panel follows the game's language, including live language changes. See [supported languages and font compatibility](docs/LOCALIZATION.md).

## Connecting

The default port is **7780**. For same-computer testing, a guest connects to `127.0.0.1`. For a trusted LAN or secured VPN, use the host's reachable IPv4 address. Allow only the necessary private-network access in Windows Firewall; do not open a public internet port.

The transport is unencrypted and has no lobby password. Real host/join/leave and edited endpoints have been tested in the game against separate test peers on loopback. Connections between two computers have not yet been verified.

IP and port fields support selection, cursor movement, deletion, and copy/paste. F6 hides or reopens the panel without leaving the session. **Leave** disconnects; closing the game also ends its session. Connecting does not start a shared run.

See the [Windows package guide](distribution/README.md) for files, cache locations and integrity checks. Vortex deployment remains experimental and is not certified; see the [distribution guide](docs/DISTRIBUTION.md).

## Planned gameplay

The intended co-op mode uses a shared world with synchronized enemies and health, while each player keeps their own progression and upgrade choices. The host will be able to fill vacant slots with bots and choose between spectating after death or ending the run on the first death.

Game synchronization, in-game bots, coordinated pauses, applying XP/pickup rules to game objects and progress-preserving run reconnect are not implemented. The separate developer lobby has additional rule/loadout controls, but those controls do not configure a running game. The [in-game menu guide](docs/INGAME_MENUS.md) explains the intended campaign-to-lobby flow and the boundary between menu browsing, network liveness and gameplay pauses.

## Development

Build from source with the .NET 10 SDK, PowerShell 7, Python 3 and the Windows C++ build tools described in the [developer guide](docs/DEVELOPMENT.md). From the repository root:

```powershell
pwsh -File tools/verify.ps1
```

The script builds the projects and runs native, network, launcher and Windows Forms tests. The GUI tests require an interactive Windows desktop. Automated checks cover the tools and libraries; gameplay needs separate testing once the game adapter exists.

The source tree also contains experimental player-input, upgrade-selection and gameplay-transport adapters. They are developer components, not playable multiplayer controls, and are not enabled in the packaged connection preview. See the [native adapter limits](native/README.md#experimental-player-adapters). The checked-in Windows package is not automatically rebuilt when these sources change.

Component documentation:

- [Legacy developer lobby application](src/Wsm.App/README.md)
- [Developer lobby API](src/Wsm.Lobby/README.md)
- [Session and progression API](src/Wsm.Core/README.md)
- [Native inspection bridge](native/README.md)

The in-game connection panel has been tested with game **2.1.6**, Unity **6000.0.74f1** and IL2CPP metadata **31** (Steam app **3669620**). Other game versions have not been tested. See the developer guide for [inspection and runtime preparation](docs/DEVELOPMENT.md#inspect-and-prepare) and [game-probe save precautions](docs/DEVELOPMENT.md#game-probes-and-save-data).

## Help and contributions

Report bugs or suggest changes through [GitHub Issues](https://github.com/Noa3/WarhammerSurvivorsMultiplayer/issues). Include the application revision, Windows and game versions, reproduction steps and the error message. Remove personal information and session credentials from any attached logs.

For code changes, run the verification script and describe which behavior you tested. Keep game files and saves out of contributions.

This is an unofficial project, unaffiliated with the game's developers or rights holders. Game diagnostics require your own installed copy. Third-party runtime license notices are included in the [Windows package](distribution/win-x64/licenses/).
