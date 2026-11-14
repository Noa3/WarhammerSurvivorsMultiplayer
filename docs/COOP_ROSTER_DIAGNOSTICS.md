# Experimental roster diagnostics

This developer-only bridge mode tests the installed game's player enrollment
and menu-selection contracts. **It changes game state.** It does not connect
network players, provide bots or deliver playable multiplayer. The ordinary
`Start-WSM.cmd` / `play` path does not enable it and clears an inherited roster
command directory.

The current contract was exercised with Warhammer Survivors 2.1.6, Steam build
26581996, Unity 6000.0.74f1 on Windows x64. It is an experimental internal game
contract, not an officially supported mod API. Missing or incompatible required
metadata stops initialization; no hard-coded object or method offsets are used.

## Ownership and safety

Use only an explicitly owned child started through `ChildBootstrap.Start`.
Keep the normal profile and Steam Cloud risk separate from the copied game
installation: **they are not isolated**. Obtain permission for a normal-profile
test and make a verified, game-scoped backup first. Do not run two copies against
the same writable profile. Do not restore an older backup automatically.

A trusted harness must create a fresh, empty absolute command directory outside
the installation and runtime cache, set `WSM_COOP_ROSTER_DIR` for its child and
request the bootstrap's explicit `mainThreadProbe`. Other in-game UI, player
observation, legacy local-coop and secondary observation-signal modes must be
disabled. Wait for `WSM_ROSTER_READY` before submitting requests. There is no
new product CLI command that provides this experiment automatically.

The caller must control the directory and keep its ancestors stable. Startup
path/reparse checks and exclusive file publication are not protection against
hostile directory replacement or another local writer.

Before **every mutation**, visually establish a menu/outside-run phase. The
bridge also refuses mutations when its player reader captures an active run or
returns an error. `NotReady` alone is not proof of a safe phase: failed run
initialization can also produce it. Never attempt cleanup mutations inside a
failed loading or upgrade screen merely because the reader is unavailable.

All engine access runs on the independently verified Unity main thread.
Operations must be serialized and remain within the runtime's lifetime. The
metadata worker detaches before waiting for commands. A callback already running
is not preempted by its dispatch timeout.

## Bounded command protocol

Submit regular files `request-000001` through `request-000008` in order. Each
contains one ASCII command without a trailing newline. Publish a completed
request atomically instead of exposing a partially written file. The session
accepts at most eight requests within 15 minutes after readiness; each pending
callback has a two-second dispatch timeout. Invalid commands or a mismatched PID
stop the diagnostic loop.

| Command | Effect |
| --- | --- |
| `inspect` | Capture configured/enrolled IDs, optional menu configuration and current player observation. |
| `enroll:<pid>:<id>` | Add one existing configured player to the exact one-player baseline. |
| `select:<pid>:<id>` | Select an enrolled menu player, not a hero; no exclusive input control or vibration. |
| `copy:<pid>:<id>` | Copy the stock menu's currently bought character into that player's existing selection entry. |
| `rollback:<pid>` | Remove only this adapter's owned addition after the original menu player is restored. |

`pid` must identify that owned game process; `id` is an observed Rewired player
ID, not a presumed roster index. The adapter supports one owned addition to a
one-player baseline, not a general party editor.

Responses are `state-000001.json` etc. The envelope reports `action`,
`actionSucceeded`, roster `state`, `pendingEnrollment`, `configuredIds`,
`enrolledIds`, `characterConfiguration`, `playerObservation` and separate
`errors.action`, `errors.roster`, `errors.characterConfiguration` and
`errors.playerObservation`. Optional diagnostic failure is not an empty successful
selection. Inspect each subsystem's result; a successful action does not certify
all observations. Existing outputs are never replaced.

`characterConfiguration.selectedTypes` is the result of the game's
`GetCharacterSelections()` method, not a direct dictionary-field snapshot.
`coopConfigReferencePresent` establishes only a managed reference, not a valid
native asset or correct behavior. Getter names do not establish purity.

## Mutation boundaries

Enrollment validates actual public `AddPlayer(Rewired.Player)` metadata. Exact
rollback uses the game's **private** `RemovePlayer(Rewired.Player)` contract;
a public generated wrapper is not evidence of public native visibility. Both
signatures, declaring types and access flags are validated before use.

The original manager, player objects and ordered roster are pinned and checked.
An unexpected manager or membership change refuses rollback rather than deleting
foreign players. Restore the original canonical selected player and index zero
before removal; direct rollback while the added player is selected is refused.
An unrolled-back owner retains bounded roots until process exit instead of making
hidden engine calls from its destructor.

Character copy validates the scalar stock-menu enum, `IsBought(character,false)`,
existing canonical player/selection dictionary types and a writable public
`CoopSelections.SelectedCharacter` field. It neither creates keys/entries nor
calls buy, unlock, currency or savefile-editing APIs. Rooted-object readback and
a roster recheck are not an atomic engine transaction: unknown getters could
replace or alias configuration/entries. The native method bodies and that
reentrancy boundary are not proven safe against arbitrary changes.

**Membership rollback does not undo a copied hero choice**, native events, UI
changes or persistence. The game can persist configuration changed in memory;
absence of direct savefile writes does not establish unchanged saves.

## Verified behavior and remaining failures

In a single owned game instance, menu enrollment and exact rollback were observed.
Restoring the original menu player before removal and refusal while the added
player remained selected were also exercised. Ordinary keyboard confirmation
can reselect player zero; selecting a menu player does not by itself assign
that player's hero.

Copying distinct bought choices produced menu selections `[5012,5001]` and the
normal stage start created two actual main characters with input IDs zero and
one and different HP. This did **not** complete personal hero selection: both
actual character types were `5001`. Both advanced to level two with equal XP,
and the first upgrade displayed no choices with a `NullReferenceException` in
`WSLevelUpPage.CanCharacterReceivePass`. A direction pulse overlapped that modal,
so independent movement was not established. The owned game closed normally;
no roster mutation was attempted inside the failed upgrade state.

These observations do not prove synchronized worlds across machines, virtual
input, personal XP/upgrades, death/spectating, reconnect or save invariance.
Do not repeat the same failed run without a discriminating measurement or fix.

The [original IL2CPP fixture](../tests/UnityCoopRosterFixture/README.md) links the
production adapter and has executed 20 exact named cases plus isolated no-op
sensitivity controls. The canonical native suite covers mode isolation, command
parsing and actual JSON error serialization. Fixture methods are original test
contracts, not recovered vendor bodies; resource failures, metadata collisions
and reentrancy are not exhaustively covered.
