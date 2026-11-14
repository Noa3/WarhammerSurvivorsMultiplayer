# Player-state observation

This is a developer-only, read-only diagnostic mode in the native bridge. It reads the local game's current players; it does not create remote characters, suppress a client's simulation, replicate a world or apply progression. The normal in-game connection panel remains separate.

## Safety and activation

**A copied installation does not isolate the normal profile, saves or Steam Cloud.** Obtain explicit consent for a real-game test and back up valuable saves first. Do not run two game copies against one writable profile as an isolation test. Observing getters is not a proof that every game's native getter body is free of side effects.

The mode is intended for an owned-child harness using [ChildBootstrap.Start](../src/Wsm.Launcher/ChildBootstrap.cs), not attachment to an arbitrary running process. Before starting that child:

1. Create a fresh, empty output directory outside the installation and runtime cache. Its path and ancestors must be absolute, existing directories without reparse points.
2. Set the child environment variable `WSM_PLAYER_OBSERVATION_DIR` to that directory and enable `WSM_MAIN_THREAD_PROBE=1` through the owned bootstrap's `mainThreadProbe` argument.
3. Keep the in-game UI, local-co-op mutation and `WSM_OBSERVATION_SIGNAL` disabled. Conflicting modes fail before runtime access. The normal `play` command clears an inherited player-observation directory rather than enabling this diagnostic.
4. Wait for `WSM_PLAYER_OBSERVER_READY` in the bridge log. The metadata worker has then detached; subsequent Unity access happens only in verified main-thread callbacks.

This mode is not a new menu option or a standalone launcher command. Do not substitute the existing one-shot `ui-probe` supervisor for a harness that keeps ownership throughout the requested observations.

The output directory is a trusted, caller-managed resource. The bridge checks the
path, emptiness and reparse attributes at setup only. It does not prove directory
ownership, create an exclusive per-run directory or retain filesystem identity
through later changes. The caller must keep the directory and its ancestors
stable and permission-protected for the session. This is not a security boundary
against concurrent local directory replacement or junction manipulation; do not
use a shared or untrusted output path.

## Request and output protocol

Create a regular, non-linked file named `request-000001` in the output directory. Its contents are unused. The bridge writes `state-000001.json`, then waits for `request-000002`, up to `request-000008`. Requests are processed in sequence. Files and failed temporary outputs are retained; use a new directory for another session rather than overwriting an old result.

There is a total 15-minute request deadline after readiness and a two-second callback-dispatch timeout per request. This does not preempt a callback or native getter already executing; its owner must remain alive until execution finishes or the owned process ends. Closing the owned game ends the observer. The bridge creates each temporary output exclusively, flushes it and renames it without replacing an existing destination.

The version-1 JSON envelope contains `schemaVersion`, `sample`, `state`, `thread` and `players`. `thread` reports the observed native, managed and engine-main identifiers. A complete `captured` result additionally contains `stageType` and one to four players, each with:

- `slot`, `inputId`, `characterType`, `level`;
- finite floating-point `x`, `y`, `hp`, `xp`;
- `dead` as a Boolean.

`not_ready` and `error` results have an empty `players` array and no `stageType`; old state is not republished as current. Error details remain in the bridge log. Missing runtime APIs or incompatible required metadata stop initialization instead of producing a zero-valued success.

## Reader boundaries

[PlayerStateReader](../native/include/wsm/player_state_reader.h) requires a verified Unity thread before invoking gameplay getters. A single manager must report `IsGameRunning`; missing stage/player membership or a stopped run returns `NotReady`. Multiple managers, more than four players, invalid objects, mismatched required types or non-finite values are rejected. A failed temporary root for a non-null stage/list is an error, not ordinary lifecycle unavailability.

The reader builds a new logical snapshot and assigns the caller's output only after every player succeeds. That publication guarantee is not a claim that arbitrary native getters represent an atomic engine transaction. The runtime's provenance is trusted; the reader is not a sandbox for hostile assemblies or metadata with deliberately colliding type names.

An actual Warhammer Survivors 2.1.6 run was checked through the owned bridge on its verified Unity thread: title/menu unavailability, an active level-up selection, ordinary pause, nonzero fractional XP/HP and controlled horizontal/vertical movement. A later menu sample returned no players. In that tested scene, right increased X and up increased Y. This establishes the observed local access path, not universal world units, all campaigns, death/spectating, multiple real players or multiplayer authority.

## Automated fixture

The [original Unity player-state fixture](../tests/UnityPlayerStateFixture/README.md) compiles the same production reader against original namesake contracts. It tests typed values, real thread/context boundaries, stopped-but-populated state, malformed/late-failing input, logical output preservation and injected root failures. These SDK checks are separate from the ordinary canonical script and do not launch the installed game. The root CTest suite additionally covers mode conflicts, locale-independent observation JSON and isolated ordinary filesystem refusal/publication checks. These are not directory-swap or junction-race tests.
