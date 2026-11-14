# Warhammer Survivors Multiplayer

An independent, experimental Windows co-op project for **Warhammer Survivors**. No MelonLoader installation is required or used by this project.

> **Development checkpoint, not a playable multiplayer release.** The standalone launcher and native IL2CPP inspection have run against the real game. A successful inspection does not mean that shared enemies, remote characters, personal upgrades or spectator gameplay work. Do not install this expecting a finished co-op mod.

## Intended co-op rules

The host owns one shared world: enemy spawns, attacks, damage and run outcome. Clients own their experience and upgrade choices; one player's pickup should not remove another player's opportunity to progress. The default defeat rule is **all players dead**, allowing dead players to watch survivors. **Any death ends the run** is an optional host-selected challenge. Optional bots should have host-configurable skill and fill vacant slots rather than override human input.

These gameplay rules are the integration target, not a claim that the current native adapter implements them.

## What is currently usable

- Inspect the installed Windows x64 game, IL2CPP metadata version, file fingerprints and conflicting loader files.
- Prepare a separate runtime copy without importing root-level MelonLoader, BepInEx, proxy DLLs or existing mods. Original game files are not overwritten or hardlinked.
- Load an original native DLL into **only a child started by this launcher**; there is no arbitrary-process attachment command.
- Probe live IL2CPP metadata without requiring generated MelonLoader assemblies.
- Run an opt-in window-thread probe that invokes Unity scene enumeration through validated runtime metadata, without inline patches or guessed method-pointer layouts.
- Execute automated native and launcher regression tests, including real child-process DLL loading.
- Exercise a separate managed session library with actual TCP clients, per-client XP receipt stores, personal upgrade commands, death rules and deterministic placeholder bot inputs. **It is not wired to the game.** See [Wsm.Core](src/Wsm.Core/README.md) for API and security limits.
- Synchronize host-owned enemy HP between TCP clients and send recipient-private XP, item and artifact receipts. This verifies the protocol only: real enemy spawning, hit validation, loot pickups, inventory application and persistence are not implemented.
- Host and join an IP lobby through the [HTTP lobby library](src/Wsm.Lobby/README.md): independent loadouts, readiness, bot slots and host-only rules. The default start request explicitly refuses because no gameplay adapter exists.
- Use the [Windows lobby application](src/Wsm.App/README.md) to create/join that lobby, edit your own selections and readiness, and configure bots and defeat rules as host. A real GUI smoke test exercises separate host and guest windows over HTTP, including connection loss and clean shutdown.

Shared enemy health, private loot/artifacts, personal progression and coordinated pauses remain game-integration requirements, not completed gameplay features.

The locally exercised target is game **2.1.6**, Unity **6000.0.74f1**, IL2CPP metadata **31**, Steam app **3669620**. Other versions are not certified.

## Run the included Windows package

The repository includes the [self-contained Windows x64 package](distribution/README.md), with the required .NET runtime files and their license notices. After downloading and fully extracting the repository, run `distribution/win-x64/lobby/Wsm.App.exe`. The diagnostic launcher and native bridge are also included. This is the development lobby/tooling package, not a playable multiplayer mod.

## Build requirements

- A legally installed Windows x64 copy of the game and its normal Steam prerequisites.
- .NET 10 SDK.
- CMake 3.20 or newer and Visual Studio C++ build tools (MSVC, Windows SDK).
- Windows x64. Administrator privileges are not requested.

## Open the lobby application

```powershell
dotnet run --project src/Wsm.App -c Release
```

Create a lobby with a player name and port (default **7780**). Loopback is selected by default for local testing. For friends on a trusted LAN/VPN, explicitly select the LAN binding and share your reachable IPv4 address and port; friends choose **Beitreten** in their own application. There are four total slots including host and bots. Each human submits their own hero/weapon IDs and clicks **Bereit**. The host alone changes bot difficulty and defeat rules.

**This currently tests the lobby, not the game.** Hero/weapon IDs are unvalidated input, not a verified unlocked-game catalogue. **Start anfragen** returns an explicit refusal until the gameplay adapter is implemented. No game process is launched by this app. There is no TLS or lobby password; do not expose this port to the public internet. Loopback is tested; cross-machine/firewall/VPN connectivity is not certified.

## Connection feedback

Failed connection attempts show a German reason, the attempted IP/port and a suggested next step. The app distinguishes a refused port, timeout, full lobby, protocol mismatch, invalid session and incompatible response without claiming that every timeout is a firewall problem. Failed joins can be retried. See [connection troubleshooting](docs/CONNECTION_HELP.md).

## Build and check

From the repository root, in a terminal with these tools available:

```powershell
pwsh -File tools/verify.ps1
```

This builds the native and managed projects, runs the network/launcher harnesses, CTest and the real Windows Forms smoke test. The GUI smoke opens test windows and requires an interactive Windows desktop. It does not launch the game. Individual commands:

```powershell
cmake -S native -B artifacts/native-build -A x64
cmake --build artifacts/native-build --config Release
ctest --test-dir artifacts/native-build -C Release --output-on-failure
dotnet build src/Wsm.Launcher/Wsm.Launcher.csproj
dotnet run --project tests/Wsm.Launcher.Tests -- "$PWD/artifacts/native-build/Release"
dotnet run --project tests/Wsm.Core.Tests
dotnet run --project tests/Wsm.Lobby.Tests
dotnet run --project tests/Wsm.App.Smoke
```

The native DLL is `artifacts/native-build/Release/Wsm.Bridge.dll`. Intermediate output under `artifacts/` and project `bin/obj` folders is excluded from Git; curated project-owned distribution files and runtime assets are not excluded by extension.

## Inspect and prepare

```powershell
dotnet run --project src/Wsm.Launcher -- inspect "G:/SteamLibrary/steamapps/common/Warhammer Survivors"
dotnet run --project src/Wsm.Launcher -- prepare "G:/SteamLibrary/steamapps/common/Warhammer Survivors" "$PWD/.local/runtime"
```

Preparation needs sufficient space for a real copy of the game. It refuses existing destinations and symbolic links/junctions rather than overwriting or following them. For an updated game, use a new runtime directory. The selected installation may already contain a modified `GameAssembly.dll`; preparation excludes loaders but does **not** certify that copied game binaries are original. Use Steam's normal verification yourself when a clean source installation is needed; this project does not automatically replace your mods.

## Run a bounded native probe

```powershell
dotnet run --project src/Wsm.Launcher -- probe "$PWD/.local/runtime" "$PWD/artifacts/native-build/Release/Wsm.Bridge.dll" "$PWD/.local/logs"
```

The probe launches the separate game copy headlessly, waits for the game's managed startup, inspects metadata, reports its result, and stops its own process. Waiting only for a non-null IL2CPP domain is unsafe because that domain can exist before initialization finishes; the bridge therefore waits for the game's managed startup message.

Use `ui-probe` in place of `probe` for a bounded visible-game check of Unity scene enumeration. Its successful result confirms enumeration only: zero active characters at startup is not evidence of a playable session. The experimental `coop-probe` additionally requests the existing local-co-op entry method only if exactly one active, signature-validated target exists. It does not create a lobby, synchronize separate games or certify successful co-op. See [native probe details](native/README.md). The intended multiplayer architecture uses separate game instances, not screen streaming.

**Important:** a separate executable directory is **not save or Steam Cloud isolation**. The game can still access its normal profile and cloud services during startup. The project does not edit, reset or migrate saves, but does not promise that the game's own startup makes no writes. Back up valuable saves through your normal process before experimental gameplay testing. A copied installation is not a separate Steam account or sandbox.

## Repository boundaries

Original project source, tests, build configuration, normal user documentation and project-owned runtime data belong in Git. Distributable application/bridge files are allowed; generic `.dll`, `.exe`, `.dat`, image and archive extensions are not globally excluded. `.gitignore` excludes `IDEA.md`, internal design plans, assistant state, research/reviews, temporary build output, credentials and user saves. Original game binaries/assets and generated game interop/decompilation remain local and must come from the user's own installation, not this repository. This project is unofficial and is not affiliated with the game's developers or rights holders.

Network gameplay is intended for consenting participants in private sessions. No public matchmaking, anti-cheat bypass, hidden installation or arbitrary native-code download is provided.
