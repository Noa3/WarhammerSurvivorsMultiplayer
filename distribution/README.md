# Windows x64 development package

**This is not a playable multiplayer release.** The package contains the working lobby application, the diagnostic launcher and the native inspection bridge. Starting a co-op game, real world replication and progress-preserving run reconnection are not implemented.

## Start without building

For normal use, download `WSM-portable-win-x64.zip` from [GitHub Releases](https://github.com/Noa3/WarhammerSurvivorsMultiplayer/releases), extract its complete `WSM` folder and open `WSM/lobby/Wsm.App.exe`.

Alternatively, download the repository ZIP or clone it, extract it completely, then open `distribution/win-x64/lobby/Wsm.App.exe` relative to the repository root. Do not download just the executable. See the [distribution guide](../docs/DISTRIBUTION.md) for installation, removal and the experimental Vortex/FOMOD alternative.

Keep all accompanying files and subdirectories together. This is a self-contained .NET 10 Windows x64 distribution; a separate .NET SDK/runtime installation is not required for the packaged managed applications. Runtime licenses and third-party notices are included under `win-x64/licenses/` and must accompany redistribution.

For local testing, create a lobby and connect another application window to `127.0.0.1` on the same port. For a trusted LAN/VPN, the host must explicitly choose the LAN binding and share their reachable IPv4 address. Connection failures display a reason and next step. See [connection help](../docs/CONNECTION_HELP.md).

## Diagnostic tools

`win-x64/launcher/Wsm.Launcher.exe` provides the documented `inspect`, `prepare` and bounded probe commands. `win-x64/bridge/Wsm.Bridge.dll` is the matching native bridge. The original game and its normal Steam/Windows prerequisites are required separately. The native bridge is an MSVC build; a fresh-machine native-runtime configuration is not certified by the local tests.

Example from the repository root:

```powershell
.\distribution\win-x64\launcher\Wsm.Launcher.exe inspect "G:\SteamLibrary\steamapps\common\Warhammer Survivors"
```

`inspect` reads installation metadata without starting a game. Before running a game probe, read the [save and Steam Cloud precautions](../docs/DEVELOPMENT.md#game-probes-and-save-data).

## Integrity and rebuilding

`win-x64/package-manifest.json` records every packaged runtime file's relative path, size and SHA-256. It also explicitly marks `playableMultiplayer` as false. Download the full directory rather than just an EXE.

With the source build prerequisites installed, create a new package using:

```powershell
pwsh -File tools/package.ps1 -OutputDirectory artifacts/package-new
```

The packaging script refuses an existing output directory rather than overwriting user files. Without `-OutputDirectory`, it targets `distribution/win-x64`, which is already present in a normal checkout. Choose a new destination for each build.

See the [developer guide](../docs/DEVELOPMENT.md#build-and-test) for build requirements and automated checks.
