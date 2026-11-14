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
| `upgradeContext:<pid>` | Explicit read-only upgrade-context query in an existing run; never enters roster mutation dispatch. |
| `upgradeSelection:<pid>` | Separate opt-in context query with bounded Unity-reported container liveness and hierarchy activity. |
| `upgradeItems:<pid>` | Separate opt-in query for at most 32 direct children and their item-component/data observations in the diagnostic selected container. |
| `enroll:<pid>:<id>` | Add one existing configured player to the exact one-player baseline. |
| `select:<pid>:<id>` | Select an enrolled menu player, not a hero; no exclusive input control or vibration. |
| `copy:<pid>:<id>` | Copy the stock menu's currently bought character into that player's existing selection entry. |
| `rollback:<pid>` | Remove only this adapter's owned addition after the original menu player is restored. |

`pid` must identify that owned game process; `id` is an observed Rewired player
ID, not a presumed roster index. The adapter supports one owned addition to a
one-player baseline, not a general party editor.

Responses are `state-000001.json` etc. The envelope reports `action`,
`actionSucceeded`, roster `state`, `pendingEnrollment`, `configuredIds`,
`enrolledIds`, `characterConfiguration`, `playerObservation`, `upgradeContext` and separate
`errors.action`, `errors.roster`, `errors.characterConfiguration` and
`errors.playerObservation` / `errors.upgradeContext`. Optional diagnostic failure is not an empty successful
selection. Inspect each subsystem's result; a successful action does not certify
all observations. Existing outputs are never replaced.

`characterConfiguration.selectedTypes` is the result of the game's
`GetCharacterSelections()` method, not a direct dictionary-field snapshot.
`coopConfigReferencePresent` establishes only a managed reference, not a valid
native asset or correct behavior. Getter names do not establish purity.

### Explicit upgrade-context query

`upgradeContext:<pid>` invokes the structural context reader. Apart from the
separate selection/item queries below, ordinary commands report
`upgradeContext.state` as `not_requested`; optional Wilson/equipment metadata
is not required by normal reader initialization or observation. The new query
still counts toward the same eight-request budget and uses the verified main
thread. It does not require the menu-only mutation precondition, invoke a new
game getter, open a page or write any field.

The reader requires an active run, valid Stage and bounded MainPlayers, plus
exactly one scene `Wilson.UI.WSLevelUpPage`. Missing scene resources report
`not_ready`; ambiguous instances or incompatible metadata report `error`.
Both statuses suppress old actor/context values. A captured result reports:

- `levelUpPageCount` and the page's game-session/factory reference presence;
- active-character reference presence and `activeCharacterSlot`: an exact
  MainPlayers slot, `-1` for no active reference, or `-2` for a non-null reference
  outside that list;
- each actor's slot, input ID, character type and weapons-manager,
  accessories-manager and accessories-collection reference presence.
- the page's choice-container group/list presence and ordered managed-reference
  presence for at most eight container entries. Missing group, null list and
  present empty list are reported distinctly. Counts outside that bound refuse
  the entire capture rather than silently truncating it.

Session/equipment properties and the choice-group list are inspected through
verified fields, not their game getters. The concrete framework
`List<Transform>.get_Count/get_Item` accessors read container references;
`UIGroup.GetActiveItem`, Transform children and component active-state APIs are
not invoked. **Presence means a rooted managed reference,
not Unity native-object liveness**, valid contents or a cause of the upgrade
exception. No equipment element is read. Container reference presence does not
establish an active, live selection or correct card components. The normal previously approved scene
getters are still involved; this is not a proof of their purity or an atomic
engine snapshot. `actionSucceeded` for this command means context capture, not
that every other observation succeeded or the upgrade became playable.

In a target-game capture on version 2.1.6, the first failed two-hero upgrade
reported a present choice group and list with three ordered references
`[false, false, true]`. The preceding ordinary Level-1 pause reported the
optional context as `not_ready`. This validates the structural reader in that
flow, not the upgrade: choices remained empty and `CanCharacterReceivePass`
still threw. A missing group, null list or empty list was not observed at that
post-exception snapshot; active selection, native liveness, child components
and the exact throw-time operand remain unverified.

### Explicit Unity-reported selection query

`upgradeSelection:<pid>` is a separate opt-in operation, not an expansion of
the structural query. After the same thread/run guards it additionally resolves
the exact canonical `UnityEngine.CoreModule.dll` methods
`Object.op_Implicit(Object): Boolean`, `Component.get_gameObject(): GameObject`
and `GameObject.get_activeInHierarchy(): Boolean`. Missing or incompatible
methods refuse the query; normal scene reads and `upgradeContext` do not require
these additional bindings.

For a successful capture, `upgradeContext.choiceSelection` contains:

- `observed: true`, including a valid capture with no group/list entries;
- `unityAlive` and `activeInHierarchy`, ordered to match
  `choiceContainerItemReferences`, with false values for absent managed entries;
- `selectedContainerSlot`, the first Unity-reported live and active entry, or
  `-1` when the complete bounded observation found none.

Destroyed Unity objects can retain a managed reference while reporting false
truthiness. The GameObject/activity getters are called only for reported live
entries. All entries up to the eight-entry bound are observed, even after the
first active match; a later invocation, type or rooting error refuses the whole
capture. Errors and `not_ready` suppress all selection values instead of exposing
partial or stale flags. The query uses the existing PID and request budget and
never enters roster mutation dispatch.

This is a diagnostic selection, **not proof of the stock `GetActiveItem` result
or throw-time state**. Equivalence between `op_Implicit` and the stock native
inline check is unproven. The query does not invoke `GetActiveItem`, enumerate
Transform children, query card components, change active state or repair a page.
It contains no explicit setter/mutation call; purity of the invoked native
methods and scene stability across calls are not established. The original
IL2CPP fixture covers live, inactive and destroyed containers, first-active
ordering, absent groups, thread refusal, late-error atomicity, the entry cap and
a stopped populated run. Those tests are not target-game or gameplay evidence.

In a separate target-game 2.1.6 capture at the first empty two-hero Level-2
upgrade, the query completed with `selectedContainerSlot: 2`,
`choiceContainerItemReferences: [false, false, true]`,
`unityAlive: [false, false, true]` and
`activeInHierarchy: [false, false, true]`. The preceding ordinary Level-1 pause
reported `not_ready`. Both actual heroes still matched `[5012, 5001]`; both
reached Level 2 with equal XP. The upgrade remained empty and
`CanCharacterReceivePass` still threw. An absent, destroyed or inactive
diagnostic selected container was therefore not observed at that post-exception
snapshot. Card components/data and the exact original throw operand remain
unverified. The owned game closed normally; no in-run roster rollback or page
mutation was attempted.

### Explicit item-content observation

`upgradeItems:<pid>` invokes `read_upgrade_items`, retaining the first container
selected by the diagnostic Unity liveness/activity observation. It does not
extend the existing structural or selection-only calls. Additional bindings are
resolved only for the item query: canonical `Transform.get_childCount(): Int32`,
`Transform.GetChild(Int32): Transform` and
`Component.GetComponent(System.Type): Component` from `UnityEngine.CoreModule.dll`.
The argument is the rooted runtime type of `Wilson.UI.WSLevelUpItemUI`. Its private
instance `_type`, `_data` and `_isNew` fields must match the exact declaring
class, game image and canonical field types. `VampireSurvivors.Data.WeaponType`
must have canonical `System.Int32` as its enum base; the referenced
`VampireSurvivors.Data.Weapons.WeaponData` is an ordinary managed class.

Successful context captures include `choiceSelection` and `choiceItems`:

- No selected container: `observed: false`, `childCount: -1`, empty `items`.
  This is not a complete empty-child observation.
- A selected container with zero children: `observed: true`, `childCount: 0`,
  empty `items`.
- Otherwise, `items` is ordered by direct-child index. Each row distinguishes
  `childReferencePresent`, `childUnityAlive`, `componentReferencePresent`,
  `componentUnityAlive`, `fieldsObserved`, `weaponType`, `dataReferencePresent`
  and `isNew`. Values are meaningful only with their corresponding observed and
  presence flags. A decorative child may legitimately lack the component;
  a zero weapon type with null data is not automatically an invalid card.

Negative counts or counts above 32 refuse the entire query rather than returning
a prefix. Count changes detected during traversal, failed invocations, invalid
types, metadata or roots also refuse the complete capture. Errors and
`not_ready` emit neither item nor selection values, leaving the reader caller's
snapshot unchanged. Counts are sampled; changes that revert between samples or
same-count reordering are not detected. Only the selected container's direct
children are traversed, never descendants or another container's children.

No page operation, field setter, exception suppression or gameplay repair is
performed. `GetComponent(Type)` is not proven equivalent to the stock generic
call, and this post-exception observation cannot establish the original throw
operand or state. Invocation purity and scene stability are not established.

In one separate target-game 2.1.6 first-failed-upgrade capture, the diagnostic
selected slot 2 and observed exactly four direct children. All four reported
live Transforms. The first three returned live item components with observed
fields, weapon types `[5072, 5012, 5002]`, present data references and `isNew:
true`. The fourth returned no managed item component, so its fields were not
observed; its default zero/false values are not card data. The ordinary Level-1
pause reported `not_ready`; both actual heroes `[5012, 5001]` subsequently
reached Level 2 alive with equal XP. The empty selection and
`CanCharacterReceivePass` exception persisted. This narrows the observed
post-exception state but does not prove the fourth child is invalid, identify
the original generic argument/throw operand, or justify removing, replacing or
adding a component. The owned game and controller both exited normally.

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

## Order-dependent primary hero initialization

For the exercised version, a matching `selectedTypes` list is not sufficient
to establish the primary actor's hero. In the controlled test, the primary
actor followed the stock menu's last confirmed scalar choice, while the added
actor followed its copied selection. Copying a primary dictionary entry does
not also restore the scalar menu choice.

For the observed baseline IDs zero and one, the successful sequence was:

1. Establish the outside-run phase and enroll the configured additional player.
2. Confirm that additional player's available hero in the stock menu, then
   submit `copy:<pid>:1`.
3. Confirm the primary player's available hero **last** through the stock UI,
   then submit `copy:<pid>:0` without another hero change before stage start.
4. Start the stage normally and compare both actual actor types with the
   requested choices; do not infer success from the menu response alone.

These IDs describe the measured baseline, not a general identity/index mapping.
The adapter does not provide an automatic host-start or hero-selection screen.
This ordering establishes distinct local heroes only; it does not repair the
upgrade failure below or enable network gameplay.

## Verified behavior and remaining failures

In a single owned game instance, menu enrollment and exact rollback were observed.
Restoring the original menu player before removal and refusal while the added
player remained selected were also exercised. Ordinary keyboard confirmation
can reselect player zero; selecting a menu player does not by itself assign
that player's hero.

An initial preparation copied distinct bought choices into menu selections
`[5012,5001]`, but left the stock scalar choice at `5001`. The normal stage
start created two actual main characters with input IDs zero and one and
different HP, but both actual character types were `5001`.

A subsequent single-instance control retained the exact same bridge bytes and
changed the confirmation order only: copy additional player `5001` first,
confirm primary `5012` last, then copy the primary entry. Both the level-one and
level-two observations then contained actual actor types `[5012,5001]`, matching
the requested distinct heroes. No generator patch or forced spawn was needed.

The first upgrade still displayed no choices with a `NullReferenceException`
in `WSLevelUpPage.CanCharacterReceivePass`, and both actors reached level two
with equal XP. Distinct local heroes therefore do not resolve the upgrade or
personal-progress boundaries. Independent movement was not measured in the
order-control run; a direction pulse in the earlier run overlapped the upgrade
modal and was inconclusive. Both owned games closed normally; no roster mutation
was attempted inside either failed upgrade state.

These observations do not prove synchronized worlds across machines, virtual
input, personal XP/upgrades, death/spectating, reconnect or save invariance.
Do not repeat the same failed run without a discriminating measurement or fix.

The [original IL2CPP fixture](../tests/UnityCoopRosterFixture/README.md) links the
production adapter and has executed 20 exact named cases plus isolated no-op
sensitivity controls. The canonical native suite covers mode isolation, command
parsing and actual JSON error serialization. Fixture methods are original test
contracts, not recovered vendor bodies; resource failures, metadata collisions
and reentrancy are not exhaustively covered.
