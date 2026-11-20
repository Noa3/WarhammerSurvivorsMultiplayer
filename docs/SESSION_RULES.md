# Session rules

**Session rules do not yet change Warhammer Survivors gameplay.** The session library routes rewards over TCP, but it does not apply them to game drops, inventories or saves.

## In-game panel

In builds with host-rule controls, open **Co-op [F6]** before hosting. Choose 0–8 bot slots, Easy/Normal/Hard difficulty, and whether the run should end on the first death or only when everyone is dead. **Host** starts the session with this rule set. Guests cannot edit it; leave and host a new session to change it. Choosing a bot count does not spawn characters, and choosing a death rule does not activate spectating.

### Solo host and bots

For a solo host with bot allies the runtime rule is **last human, not last actor**: losing all bots leaves the run active, and losing the sole human ends it even if a bot is still standing. Each living bot adds one request for an extra shared enemy per reported wave, and an already processed wave never repeats. The highest reported wave ID with its enemy type stays binding through pauses and upgrade menus; omitting the wave does not forget it. On resuming, report the current wave again, because it still owes its one batch if it was first seen while not playing. An older ID, or a changed enemy type for the same ID, is refused without consuming the tick. These rules are available to a host adapter; the game panel does not yet create those heroes or enemies, or end a stock-game run based on this result.

## Separate Windows developer lobby

Change the fields, then click **Host-Regeln übernehmen**. Until then, the edits are a draft. **Bestätigte Regeln** shows the rules currently shared with guests, including bots and the defeat condition. Applying a changed rule resets human readiness so everyone can review it. Changing a value to its already applied value does not reset readiness.

| Setting | Choices and meaning |
|---|---|
| **Erfahrungspunkte** | **Persönliche XP** is the default: each player collects for their own progression. **Team-XP** gives each connected participant the full amount from one accepted collection, rather than dividing it. |
| **XP-Drop-Positionen** | **Gleiche Positionen** is the default. With personal XP, players consume the same drop independently. **Persönliche Positionen** is available only with personal XP. Team XP forces synchronized positions and one team-wide collection. |
| **Pickups** | **Persönlich** is the default. Team sharing copies eligible coins and consumable rewards. Weapons, other inventory items, artifacts and unlocks remain personal. |
| **Bots / Bot-Schwierigkeit** | The in-game panel reserves 0–8 bot slots; the separate developer lobby limits the selector to three of its four slots. Choose easy, normal or hard. Bots are not yet running in the game. |
| **Niederlage** | Choose defeat when all players are dead or when the first player dies. Spectating and game death handling are not connected yet. |

Shared enemy positions and health, and personal upgrade choices, are fixed requirements. They are not switches for separate combat worlds or a common upgrade selection. The recovery fields in the developer policy contract reserve a future reconnect policy; the application has no working run reconnect or lossless-save guarantee.

## Connection and delivery limits

Everyone should use the same application revision. The separate Windows lobby uses HTTP; the in-game session uses TCP. They perform independent version checks and are not interchangeable. Use a trusted LAN or secured VPN; neither transport is encrypted.

The lobby client maintains its admission in the background even when no menu or roster is being refreshed. This does not restore an expired session and cannot keep a crashed or suspended process alive. **Leave** deliberately ends admission; opening a personal menu must not be implemented as leaving the lobby.

The TCP session library rejects conflicting reward replays and keeps a team event's original recipients. Repeating that event cannot grant it to someone who joined later. A successful publish means a receipt was queued, not that it was acknowledged or saved. Disconnects and process exits can still lose pending rewards; do not use this development build for valuable run progress.

See [connection help](CONNECTION_HELP.md), the [session API](../src/Wsm.Core/README.md) and the [developer guide](DEVELOPMENT.md) for diagnostics and verification commands.
