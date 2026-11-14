# Developer guide

Run the commands below from the repository root on Windows x64.

## Build requirements

- .NET 10 SDK.
- PowerShell 7, using a 64-bit process.
- Visual Studio C++ build tools with MSVC and the Windows SDK.
- CMake 3.20 or newer, with support for your installed Visual Studio version.
- An interactive Windows desktop for the Windows Forms tests.

Game inspection and probes also require an installed copy of Warhammer Survivors and its normal Steam prerequisites. Building and running the automated tests does not require launching the game.

## Build and test

```powershell
pwsh -File tools/verify.ps1
```

The script builds the native bridge and managed solution, runs CTest, then runs the Core, Lobby, App and Launcher test programs, an owned-window shutdown test and archive rejection tests. Python 3 is required for the packaging tests. The GUI tests open temporary application windows. A successful run ends with `All automated development checks passed.` followed by a reminder that gameplay testing is separate.

Original Unity integration is opt-in and never points at the installed game. Build the [original Unity fixture](../tests/UnityInteropFixture/README.md), set `WSM_UNITY_FIXTURE_DIRECTORY` to its output directory (containing `Fixture.exe` and `Fixture_Data`), then run verification to include cache integrity/update tests. For the standalone launch lifecycle, also set `WSM_UNITY_FIXTURE_INSTALLATION` to a private copy of that same fixture with the executable/data directory named `WarhammerSurvivors.exe` and `WarhammerSurvivors_Data`; retain its original native runtime files. This naming only exercises the launcher's installation contract; it is not a game compatibility fixture. The test starts only this supplied original player. Omitted environment settings mean those optional checks did not run; the ordinary pass/skip totals do not imply otherwise.

To run individual checks:

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

Open the lobby from source:

```powershell
dotnet run --project src/Wsm.App -c Release
```

To build a Windows package, follow [the packaging instructions](../distribution/README.md#integrity-and-rebuilding). The native build produces `artifacts/native-build/Release/Wsm.Bridge.dll`.

## Inspect and prepare

Replace the game path below with your installation directory. `inspect` reads the executable and IL2CPP metadata, reports versions and file fingerprints, and checks the installation's runtime files.

```powershell
dotnet run --project src/Wsm.Launcher -- inspect "G:/SteamLibrary/steamapps/common/Warhammer Survivors"
dotnet run --project src/Wsm.Launcher -- prepare "G:/SteamLibrary/steamapps/common/Warhammer Survivors" "$PWD/.local/runtime"
```

`prepare` creates a separate copy from the supported game files. It preserves the original installation, refuses existing destinations and rejects symbolic links and junctions. Allow enough disk space for the copy and choose a new destination when preparing an updated game.

Preparation copies the selected game binaries; it does not verify that they are unmodified originals. If a clean source installation is needed, use Steam's verification process before preparing the copy.

## Game probes and save data

**A separate installation directory does not isolate saves or Steam Cloud.** Starting the copied game can still read and write its normal profile and cloud data. Back up valuable saves through your usual process before running a game probe. These tools do not provide a separate Steam account or a sandbox.

After preparing the runtime and building the native bridge:

```powershell
dotnet run --project src/Wsm.Launcher -- probe "$PWD/.local/runtime" "$PWD/artifacts/native-build/Release/Wsm.Bridge.dll" "$PWD/.local/logs"
```

The launcher starts the copied game headlessly, loads the bridge into its own child process, waits for managed startup, reports metadata and stops the child. The bridge waits for the managed startup message because an IL2CPP domain can exist before initialization is complete.

Use `ui-probe` in place of `probe` for a visible-game scene-enumeration check. The experimental `coop-probe` also requests the game's local-co-op entry method when exactly one active, signature-validated target is present. It may change game state. Successful metadata inspection, enumeration or method invocation does not establish a multiplayer run.

See [native probe reference](../native/README.md) for environment variables, completion markers, deadlines and error handling.
