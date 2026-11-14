# Original Unity player-state reader fixture

Standalone, original IL2CPP fixture for the read-only `wsm::PlayerStateReader`
(`native/src/player_state_reader.cpp`). It is not a game stub, not a copy of
vendor code, and not evidence of real-game compatibility. The managed types
below are original fixture contracts that reproduce the *names* the reader
resolves (`VampireSurvivors.Runtime.dll` / `PhaserPort.dll` / `Rewired_Core.dll`
/ `Unity.Mathematics.dll`, namespaces, getters, inheritance). They contain no
gameplay logic and are built from original source, not from decompiled vendor
assemblies.

Layout: `Assets/Math` (`Unity.Mathematics` assembly: original two-field `float2`
contract, not the Unity.Mathematics package), `Assets/Phaser` (`PhaserPort`
assembly: global `ArcadeSprite.position`), `Assets/Rewired` (`Rewired_Core`
assembly: `Rewired.Player.id`), `Assets/Runtime` (`VampireSurvivors.Runtime`
assembly: `GameManager.MainPlayers`/`Stage`/`IsGameRunning`, `CharacterController`, enums),
`Assets/PlayerFixture.cs` (driver, owns the expected main-thread identities),
`Native/fixture.cpp` (exports `fixture_test` and `fixture_upgrade`, drives the
production scene reader and explicit context/selection diagnostics).

Build and run from the repository root (Git Bash), Unity 6000.0.74f1 Windows
IL2CPP module + MSVC x64:

```bash
cmake -S tests/UnityPlayerStateFixture/Native -B .local/player-state-v1-native -A x64
cmake --build .local/player-state-v1-native --config Release
mkdir -p .local/player-state-v1-work/Assets/Plugins/x86_64
cp -r tests/UnityPlayerStateFixture/Assets tests/UnityPlayerStateFixture/Packages \
      tests/UnityPlayerStateFixture/ProjectSettings .local/player-state-v1-work/
cp .local/player-state-v1-native/Release/fixture.dll \
   .local/player-state-v1-work/Assets/Plugins/x86_64/fixture.dll
WSM_FIXTURE_OUTPUT='E:/WarhammerSurvivorsMultiplayer/.local/player-state-v1-player4/Fixture.exe' \
'C:/Program Files/Unity/Hub/Editor/6000.0.74f1/Editor/Unity.exe' -batchmode -nographics -quit \
  -projectPath 'E:/WarhammerSurvivorsMultiplayer/.local/player-state-v1-work' \
  -executeMethod BuildPlayerFixture.Build \
  -logFile 'E:/WarhammerSurvivorsMultiplayer/.local/player-state-v1-build.log'
python tests/UnityPlayerStateFixture/run_fixture.py
```

`run_fixture.py` runs the player headless with `-batchmode -nographics` (window
never activated, no input touched) and needs both exit code 0 and the driver's
`PLAYER_TEST RESULT failures=0` line plus exactly one passing occurrence of
every named case in `EXPECTED_CASES`, including the 17 baseline scene cases
and the context/selection cases below, so an early exit or changed case membership
cannot look green. It defaults to
`.local/player-state-v1-player4`, the output path in the example above. Rebuild
that player from the current sources before running it, or choose a fresh path
and override `WSM_PLAYER_STATE_PLAYER`. The editor run is headless and writes
only into `.local/` (`player-state-v1-*`); check `PLAYER_FIXTURE_BUILD=Succeeded`
in the build log, not only the editor exit code.

This fixture player is a Development build (like the sibling fixtures), so Unity
starts its PlayerConnection listener and Windows Defender Firewall may prompt
for that exe path. It needs no network; declining the prompt is harmless. To
avoid the prompt, a separate non-development build can omit `BuildOptions.Development`
in `Assets/Editor/Build.cs`; its stripping behavior must be rechecked. No firewall
permission or rule is needed for this fixture.

Runtime facts observed in the real IL2CPP player (not assumptions):

- `UnityEngine.Object.FindObjectsOfType(typeof(GameManager))` returns
  `VampireSurvivors.Framework.GameManager[]`, not the declared
  `UnityEngine.Object[]`. The reader therefore validates the array by element
  comparability (`il2cpp_class_get_element_class` + `il2cpp_class_is_assignable_from`)
  instead of comparing against the materialised `Object[]` class.
- `SynchronizationContext.Current` on the Unity main thread is
  `UnityEngine.UnitySynchronizationContext` even in `-batchmode -nographics`,
  so the production thread guard is exercised for real in headless mode.
- A Windows GUI player has no console: `printf` from the native side is invisible,
  so `fixture.cpp` exports `fixture_reason()` and the driver logs the reader's
  last decision (`status` + detail) on every failure.

`link.xml` preserves the fixture assemblies and the reflection targets the
reader needs (`UnityEngine.Object`, `UnityEngine.GameObject`, `UnitySynchronizationContext`,
`System.Array`, `System.Threading.*`). Stripping in a real game differs.

Checks performed by `Assets/PlayerFixture.cs` (all against the production
reader, no reimplementation):

- `no_manager`, `null_stage`, `empty_players`, `populated_stopped` → `NotReady`.
  The last case retains a valid manager, stage and two players but reports no active run.
  Every failure/NotReady case seeds two distinctive existing players and compares
  the entire logical snapshot, not raw `std::vector` bytes.
- `two_players` → `Captured` plus exact per-slot values: stage type, input id,
  character type, level, `x`, `y`, hp, xp, dead — `x`/`y` come from the boxed
  `float2` fields read with `il2cpp_field_get_value`, and the two coordinates
  are deliberately different so an x/y mix-up is caught.
- `wrong_native_thread`, `wrong_managed_thread`, `null_context`,
  `substituted_context`, `worker_thread` → `Error` (fail-closed before any
  gameplay getter is invoked; the worker case is a real second OS thread).
- `over_cap` (5 members), `non_finite` (NaN hp), `late_non_finite` (second player)
  and `null_input` → `Error`; no partial result replaces the existing snapshot.
- `stage_root_failure`, `players_root_failure` force a non-null scene object's
  temporary root to fail under `WSM_PLAYER_STATE_TESTING`. They check failure
  classification, not an actual out-of-memory condition. This test seam is not
  compiled into the production bridge.
- `recovered` → `Captured` again after the mutation is undone.

The explicit `read_upgrade_context` entry point additionally checks the second
actor's rooted identity, foreign/null active character, null session/factory,
different actor manager references, null accessories collection, missing or
duplicate page, duplicate actor identity, stopped run, wrong native thread,
late input failure, Stage root failure, missing manager and recovery. Failed or
unavailable captures must preserve every field of a seeded two-actor context.
These are managed-reference observations, not Unity native-object liveness.

`upgrade_wrong_collection_type` uses the test-only
`test_use_wrong_equipment_collection` seam to redirect collection inspection
to an actual fixture `List<Int32>` field on the same canonical equipment manager.
It requires metadata refusal rather than accepting another `List<T>`. No runtime
metadata is patched, and this field-redirection seam is absent from the
production bridge. Equipment elements are never read by the diagnostic.

The choice-container cases use the original fixture's `Wilson.UI.UIGroup<T>`
with a protected `List<T> items` and the page's typed `_ChoiceContainerGroup`
field. They distinguish the usual absent group from a null list and an empty
list, preserve ordered `[false, true]` managed-reference presence, accept eight
entries, and reject nine with the exact bounded-count error. Every failure
oracle also compares the seeded group/list flags and entry vector. The reader
checks the closed `UIGroup<Transform>` / `List<Transform>` metadata and canonical
Transform return identity before using the framework list accessors.

The structural query does not call `UIGroup.GetActiveItem`, native liveness/active-state
APIs, Transform child enumeration or component queries. A present container
reference does not establish an active selection or valid upgrade-card contents.

The separate explicit `read_upgrade_selection` query adds canonical
`Object.op_Implicit`, `Component.get_gameObject` and
`GameObject.get_activeInHierarchy` calls on the guarded Unity main thread.
Named `upgrade_selection_*` cases verify live/active, inactive and destroyed
containers, first-active ordering with a later active entry, an absent group,
wrong-thread refusal, a later actor failure after observing the containers,
the entry bound and a stopped populated run. The error/NotReady oracles compare
all seeded flags, selection index and ordered vectors as well as actor/context
values. The ordinary structural query must reset these additional observations
to unrequested defaults on a successful capture, without requiring the extra
Unity method bindings.

`UnityEngine.GameObject` is preserved explicitly for reflective access to
`get_activeInHierarchy`. Before this preservation, the additional query refused
its missing/invalid method metadata while all existing 38 cases still passed;
the production metadata checks were not relaxed. Actual stripping in the target
game remains a separate compatibility question.

Both queries reject overlarge lists rather than returning a partial negative.
The selection query observes every bounded entry and reports the first live
and active match; it is not claimed equivalent to the stock native selection
or its timing. No Transform children, card components or card data are observed.

The reader trusts the inspected runtime's provenance. It resolves named contracts
and checks important class/image identities, but is not an admission boundary for
hostile metadata or arbitrary foreign assemblies with colliding type names.

Limitations: this fixture proves only its own metadata and state transitions. It does
not prove the game's live `MainPlayers` membership, ordering, that
`ArcadeSprite.position` is authoritative world space, its units, or that any
getter is side-effect free in the real game — a `get_` name is not proof of a
pure native body. No game process was attached, no network or UI integration
exists, and nothing here writes game state.
