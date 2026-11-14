# In-process connection runtime

`Wsm.InGame.Runtime` publishes a Windows x64 Native AOT shared library. The native game panel loads it next to `Wsm.Bridge.dll`; network workers run inside the game process, independently of Unity menu and simulation ticks. No separate lobby process is required by this entry point.

This is the connection layer only. It reuses `Wsm.Core` protocol 4, but does not apply remote input, snapshots, rewards or bots to game objects. A connected session is not a shared multiplayer run. Use only a trusted LAN or secured VPN: the current connection has no password or encryption.

## Native interface

The C declarations and result codes are in [native/wsm_ingame_abi.h](native/wsm_ingame_abi.h). ABI version 1 exposes version, host, IPv4 join, leave and a bounded UTF-8 status query. Additional state, error-category, peer-count and bound-port exports let the native panel localize status without parsing human-readable strings.

Host, join and leave enqueue work rather than blocking the Unity thread on sockets or resource cleanup. The controller owns admission, cancellation and disposal; a newer departure invalidates an older queued start. Status queries do not wait for network operations. The shutdown export is for controlled teardown/tests and must not be called from an interactive Unity callback.

Keep the native library loaded for the process lifetime. Native AOT initialization executes code, so only load the project's trusted packaged artifact; ABI validation is not a sandbox for arbitrary DLLs.

## Build and tests

From the repository root with the Windows development prerequisites:

```powershell
dotnet publish src/Wsm.InGame.Runtime -c Release -r win-x64 -o artifacts/ingame-runtime
dotnet run --project tests/Wsm.InGame.Runtime.Tests -c Release
python tests/Wsm.InGame.Runtime.Tests/dll/run_dll_tests.py --dll artifacts/ingame-runtime/Wsm.InGame.Runtime.dll
python tests/Wsm.InGame.Runtime.Tests/dll/lifecycle_test.py --dll artifacts/ingame-runtime/Wsm.InGame.Runtime.dll
```

The canonical [verification script](../../tools/verify.ps1) builds the native consumer and runs the real TCP/controller, ABI and lifecycle checks. Source-generated wire JSON is exercised by `tests/Wsm.AotWire.Tests` with reflection disabled. Those tests do not establish Unity rendering, game compatibility, LAN reachability or multiplayer gameplay.
