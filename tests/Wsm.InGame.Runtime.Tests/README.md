# Wsm.InGame.Runtime tests

Three layers, all against real loopback TCP — no stubbed protocol responses:

| Layer | What it proves | Command |
| --- | --- | --- |
| `Program.cs` (JIT) | Controller state machine, failure categories, cleanup, retry, leave-during-connect, status text | `dotnet run -c Release --project tests/Wsm.InGame.Runtime.Tests/Wsm.InGame.Runtime.Tests.csproj` |
| `dll/run_dll_tests.py` | The published Native AOT DLL through the frozen C ABI, in separate processes | see below |
| `dll/probe.cpp` | A real C/C++ consumer: `LoadLibrary` + `GetProcAddress`, full buffer/argument contract | built by `dll/CMakeLists.txt` |

## Native DLL

```text
dotnet publish src/Wsm.InGame.Runtime/Wsm.InGame.Runtime.csproj -c Release -r win-x64 \
  --artifacts-path E:/WarhammerSurvivorsMultiplayer/.local/ingame-runtime-v1-build
copy .local\ingame-runtime-v1-build\publish\Wsm.InGame.Runtime\release_win-x64\Wsm.InGame.Runtime.dll \
     .local\ingame-runtime-v1-native\
cmake -S tests/Wsm.InGame.Runtime.Tests/dll -B .local/ingame-runtime-v1-build/probe -A x64
cmake --build .local/ingame-runtime-v1-build/probe --config Release
python tests/Wsm.InGame.Runtime.Tests/dll/run_dll_tests.py
```

The DLL is self-contained (no .NET installation, no extra product EXE) and cannot be unloaded:
a host process keeps it mapped for its lifetime. `wsm_ingame_shutdown` exists for test teardown.

`--artifacts-path` keeps every managed and native artifact under `.local/` so the shared
repo build outputs are never touched.

## Unity IL2CPP coexistence fixture

`tests/UnityInGameRuntimeFixture` is an owned player build that P/Invokes the same published
DLL from a `MonoBehaviour`, hosts a real session inside the IL2CPP process and waits for an
external guest process to join.

```text
copy .local\ingame-runtime-v1-native\Wsm.InGame.Runtime.dll \
     tests\UnityInGameRuntimeFixture\Assets\Plugins\x86_64\
set WSM_RUNTIME_OUTPUT=E:/WarhammerSurvivorsMultiplayer/.local/ingame-runtime-v1-unity/Fixture.exe
"C:/Program Files/Unity/Hub/Editor/6000.0.74f1/Editor/Unity.exe" -quit -batchmode \
  -projectPath tests/UnityInGameRuntimeFixture -executeMethod BuildRuntimeFixture.Build \
  -logFile .local/ingame-runtime-v1-unity-build.log
python tests/UnityInGameRuntimeFixture/run_fixture.py
```

Exit code 0 and `FIXTURE=PASS` mean the IL2CPP player hosted the AOT DLL and the guest joined.
The fixture is a build/run probe only: it starts no gameplay run and writes no saves.
