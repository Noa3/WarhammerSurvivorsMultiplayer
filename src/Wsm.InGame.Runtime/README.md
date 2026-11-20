# In-process connection runtime

`Wsm.InGame.Runtime` publishes a Windows x64 Native AOT shared library. The native game panel loads it next to `Wsm.Bridge.dll`; network workers run inside the game process, independently of Unity menu and simulation ticks. No separate lobby process is required by this entry point.

This layer uses `Wsm.Core` protocol 5. It does not apply remote input, snapshots, rewards or bots to game objects. A connected session is not a shared multiplayer run. Use only a trusted LAN or secured VPN: the current connection has no password or encryption.

## Native interface

The C declarations and result codes are in [native/wsm_ingame_abi.h](native/wsm_ingame_abi.h). ABI version 1 exposes version, host, IPv4 join, leave and a bounded UTF-8 status query. Additional state, error-category, peer-count and bound-port exports let the native panel localize status without parsing human-readable strings.

Host, join and leave enqueue work rather than blocking the Unity thread on sockets or resource cleanup. The controller owns admission, cancellation and disposal; a newer departure invalidates an older queued start. Status queries do not wait for network operations. The shutdown export is for controlled teardown/tests and must not be called from an interactive Unity callback.

Keep the native library loaded for the process lifetime. Native AOT initialization executes code, so only load the project's trusted packaged artifact; ABI validation is not a sandbox for arbitrary DLLs.

## Observed bot decisions (host adapter)

Unlike the [snapshot simulation](../Wsm.Core/README.md#simulated-bots), the host-local `botFrame` request makes a decision from the supplied actor position on **each** frame. If a wall stopped the actor, report where it stopped; do not advance it to a predicted position. A human guest is not required. Bot capacity and difficulty come from the active host settings. This request does not travel over TCP or reserve a human slot.

```json
{"kind":"botFrame","requestId":7,"bot":{"run":1,"slot":0,"tick":42,"primary":"human","phase":"playing","actor":{"id":"ally","x":4,"y":2,"alive":true},"weaponRange":5,"maxStep":1,"enemies":[{"id":"orc","x":12,"y":2,"alive":true}]}}
```

Use a positive, increasing `run` per stage, a zero-based configured bot `slot` (not a controller number), a strictly increasing `tick` per slot, and stable actor IDs per run. Supply the actual equipped weapon range, full-speed stride for this decision interval and known enemies (up to 256); if observations are incomplete, do not submit a playing frame. An optional `personalPickup` uses the same entity shape and must already be known collectible by this actor. No drop or XP is awarded by this request.

In `paused` or `upgrade` the bot stays still. For `upgrade`, an optional `offer` has `owner`, `level` and a nonempty `choices` array of currently selectable item IDs. The bot suggests one of these only for its own live actor. It does not submit a selection to the game. For each accepted request, poll the correlated `submission`: failure has `error`; success has `botDecision` with `run`, `slot`, `tick`, `actor`, movement axes `x`/`y`, and optionally `attackTarget` or `level`/`choice`. Identity and eligibility in the observation are the trusted adapter's responsibility. Never apply a stale decision, one from another epoch/actor, or a choice for a closed or changed offer. One decision does not establish a continuing input lease. The shipped game panel has no stock actor adapter for these requests.

For `botFrame` with more than one configured bot, include `team`, the ordered list of **all** bot actor IDs. `team[slot]` must match the acting bot. The roster cannot change within a run, and no ally may appear as an enemy or a pickup, even before its first individual observation. Older clients that omit the team for multi-bot observations are refused, not guessed.

### One human with bot allies

A host with no network guests can instead submit `kind: "soloParty"` and a complete `party` observation for each tick. This also works when all bots have died. `party` contains `run`, `tick`, `phase`, `human: {id,hp,maxHp}`, an ordered `bots` array, and one shared `enemies` array. Each bot supplies its own `actor`, `hp`, `maxHp`, `weaponRange`, `maxStep`, and optionally its own `personalPickup` or `offer`. Roster identities and observation mode are fixed for the run. `actor.alive` must agree with its HP. Report only a complete local party, never a disconnected guest as a bot.

The correlated `submission.partyDecision` returns the human and bot HP observations, one decision per bot, `runEnded`, and `spawns`. Loss of the only human ends the run even if bots are alive; bot deaths alone never do, regardless of the multiplayer defeat setting. An ended run stays ended. A higher run ID starts a fresh party. Do not treat this result as a command to stop a network session.

An optional `wave: {id,enemyType}` identifies an actual base-game spawn wave. On its first playing tick, the host requests **one extra enemy per living bot** near that bot's position. A normal base wave is not replaced. The order key is `(session epoch, run, wave, slot)`; the `anchor` is not an attack-ownership restriction. Enemies belong to one shared world, not to private bot battles. The adapter must choose a safe spawn location, respect game caps, and confirm creation before reporting that enemy. Repeated waves, pauses, and ended runs issue no extra orders. These are local decisions, not Unity spawns, stock HP mutations or a stock game-over transition.

The latest observed wave ID and enemy type remain binding during pauses and upgrade menus. Omitting `wave` does not forget it or trigger a spawn. On resuming, send the current wave again: it can produce one batch if it has not already been processed. An older ID or a changed type for the same ID is refused without consuming the tick.

## Build and tests

From the repository root with the Windows development prerequisites:

```powershell
dotnet publish src/Wsm.InGame.Runtime -c Release -r win-x64 -o artifacts/ingame-runtime
dotnet run --project tests/Wsm.InGame.Runtime.Tests -c Release
python tests/Wsm.InGame.Runtime.Tests/dll/run_dll_tests.py --dll artifacts/ingame-runtime/Wsm.InGame.Runtime.dll
python tests/Wsm.InGame.Runtime.Tests/dll/lifecycle_test.py --dll artifacts/ingame-runtime/Wsm.InGame.Runtime.dll
```

The canonical [verification script](../../tools/verify.ps1) builds the native consumer and runs the real TCP/controller, ABI and lifecycle checks. Source-generated wire JSON is exercised by `tests/Wsm.AotWire.Tests` with reflection disabled. Those tests do not establish Unity rendering, game compatibility, LAN reachability or multiplayer gameplay.
