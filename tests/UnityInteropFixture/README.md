# Original Unity IL2CPP interop fixture

Standalone original fixture, not a game stub or evidence of real-game compatibility. Tested with Unity 6000.0.74f1 Windows IL2CPP, MSVC x64. Unity-generated runtime outputs are local-only and must not be distributed. `link.xml` deliberately preserves metadata; stripping in a real game may differ.

From the repository root in Git Bash, with the matching Unity Windows IL2CPP module and MSVC installed:

```bash
cmake -S tests/UnityInteropFixture/Native -B .local/fixture-v3-native -A x64
cmake --build .local/fixture-v3-native --config Release
mkdir -p .local/fixture-v3-work/Assets/Plugins/x86_64
cp -r tests/UnityInteropFixture/. .local/fixture-v3-work/
cp .local/fixture-v3-native/Release/fixture.dll .local/fixture-v3-work/Assets/Plugins/x86_64/fixture.dll
'C:/Program Files/Unity/Hub/Editor/6000.0.74f1/Editor/Unity.exe' -batchmode -nographics -quit -projectPath 'E:/WarhammerSurvivorsMultiplayer/.local/fixture-v3-work' -executeMethod BuildFixture.Build -logFile 'E:/WarhammerSurvivorsMultiplayer/.local/fixture-v3-build.log'
'E:/WarhammerSurvivorsMultiplayer/.local/fixture-v3-player/Fixture.exe' --fixture-test -logFile 'E:/WarhammerSurvivorsMultiplayer/.local/fixture-v3-positive.log'
'E:/WarhammerSurvivorsMultiplayer/.local/fixture-v3-player/Fixture.exe' --fixture-test --fixture-force-fail -logFile 'E:/WarhammerSurvivorsMultiplayer/.local/fixture-v3-forced-failure.log'
```

Set `WSM_FIXTURE_OUTPUT` to a fresh absolute `Fixture.exe` path before the editor command to select a different build output and preserve an older fixture. Otherwise the builder uses `../fixture-v3-player/Fixture.exe` relative to the copied project.

The second player run deliberately fails and exits 1. Without `--fixture-test`, the visible player exits after 60 seconds; `--fixture-test` exits after ten Update frames. Fixture assertions report failure via nonzero exit. Unity's build log, not merely editor exit code, should be checked for `FIXTURE_BUILD_RESULT=Succeeded`.

Native code resolves exports from the loaded `GameAssembly.dll`; uses reflection metadata and `runtime_invoke`, never MethodInfo/object layout offsets; type-checks boxed primitive values before unboxing; unboxes the boxed `Vector3` as the value-type receiver for `get_Item`; samples the first 16 elements of a 23-element closed generic list and verifies their values. Wrong-thread rejection occurs at the Windows thread ID check, before context inspection. The actual UnitySynchronizationContext `get_MainThreadId` is preserved and invoked, not a fixture substitute. The native fixture also builds the production `MainThreadGuard` and checks it from a real Unity callback: the valid main thread passes, while wrong native/managed identities and null/substituted synchronization contexts are rejected. These guard checks are separate from real-game compatibility and networked gameplay.
