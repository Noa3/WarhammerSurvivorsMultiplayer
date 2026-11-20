# Windows x64 connection preview

**This is not a playable multiplayer release.** The package contains the native in-game connection panel, local camera preferences, its in-process network runtime and a diagnostic launcher. The separate lobby application is retained as a developer tool. Starting a co-op game, real world replication and progress-preserving run reconnection are not implemented.

## Start without building

Download and extract the complete repository ZIP or clone it. Start `distribution/win-x64/Start-WSM.cmd`. Back up your saves before accepting the warning. Confirm the detected Steam folder if offered, or enter the installed game folder manually. Older tools-preview assets on [GitHub Releases](https://github.com/Noa3/WarhammerSurvivorsMultiplayer/releases) do not contain the newer in-game interface. A standalone portable archive uses `WSM/Start-WSM.cmd` after extraction.

Do not download just an executable. See the [distribution guide](../docs/DISTRIBUTION.md) for installation, removal and the experimental Vortex/FOMOD alternative.

If you previously set the optional `WSM_GAME` environment variable and then moved the game, update or unset it before restarting the launcher. An explicit path takes precedence over Steam discovery; the launcher will not silently launch a different copy.

Keep all accompanying files and subdirectories together. This is a self-contained .NET 10 Windows x64 distribution; a separate .NET SDK/runtime installation is not required for the packaged managed applications. Runtime licenses and third-party notices are included under `win-x64/licenses/` and must accompany redistribution.

Open **Co-op [F6]** in the launched game. Host a port or join an IPv4 endpoint. Hosting listens on network interfaces; use only a trusted LAN/VPN and necessary private-network firewall access. For local testing, connect a separate peer to `127.0.0.1`. The connection UI follows the game language; see [localization](../docs/LOCALIZATION.md). See [connection help](../docs/CONNECTION_HELP.md).

The panel offers **Own camera** by default and an optional **Shared camera** following living actors. These preferences do not create remote players or separate client views. Zoom and bounds remain under the game's control; exact stock single-player framing after a death is not verified.

The panel and compact player list use Steam display names when available, with a generic fallback. Names are presentation, not Steam account authentication. Use a matching build on all peers: the name-aware protocol is version 5 and cannot connect to protocol-4 previews. See [player names](../docs/PLAYER_NAMES.md).

## Game copies and saves

The entry point stores verified installation copies under `%LOCALAPPDATA%/WarhammerSurvivorsMultiplayer/cache` and logs under its `logs` directory. These survive removal of the extracted package. Close the owned game before reviewing or removing those copies. **Normal saves, the user profile and Steam Cloud are not isolated**; back up valuable data before launch. Do not remove the game's save folders as part of uninstall.

## Diagnostic tools

`win-x64/launcher/Wsm.Launcher.exe` provides the documented `inspect`, `prepare` and bounded probe commands. `win-x64/bridge/Wsm.Bridge.dll` is the matching native bridge. Its adjacent `Wsm.InGame.Runtime.dll` is the self-contained Native AOT network runtime; keep both files together. The original game and its normal Steam/Windows prerequisites are required separately. The native bridge requires the [Microsoft Visual C++ x64 runtime](https://aka.ms/vc14/vc_redist.x64.exe) (`MSVCP140.dll`, `VCRUNTIME140.dll` and `VCRUNTIME140_1.dll`). It may already be installed with the game. Install the official runtime if these libraries are missing; do not obtain DLLs from arbitrary download sites. A fresh-machine setup remains unverified.

Example from the repository root:

```powershell
.\distribution\win-x64\launcher\Wsm.Launcher.exe inspect "G:\SteamLibrary\steamapps\common\Warhammer Survivors"
```

`inspect` reads installation metadata without starting a game. Before running a game probe, read the [save and Steam Cloud precautions](../docs/DEVELOPMENT.md#game-probes-and-save-data).

## Integrity and rebuilding

`win-x64/package-manifest.json` records every packaged runtime file's relative path, size and SHA-256. It explicitly marks `inGameConnectionUi` true and `playableMultiplayer` false. Download the full directory rather than just an EXE.

With the source build prerequisites installed, create a new package using:

```powershell
pwsh -File tools/package.ps1 -OutputDirectory artifacts/package-new
```

The packaging script refuses an existing output directory rather than overwriting user files. Without `-OutputDirectory`, it targets `distribution/win-x64`, which is already present in a normal checkout. Choose a new destination for each build. A failure during final copying or notice/manifest generation can leave an incomplete output directory. Preserve it for diagnosis and choose a new destination; do not distribute a folder lacking a successfully verified manifest.

See the [developer guide](../docs/DEVELOPMENT.md#build-and-test) for build requirements and automated checks.
