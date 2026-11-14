# Session rules

The host configures these rules in the Windows lobby. Guests can read them but cannot change them. **The game adapter is not implemented: these settings do not yet change Warhammer Survivors gameplay.** The session library implements reward routing over TCP; world drops, inventories and saves are separate unfinished work.

## Choose and apply rules

Change the fields, then click **Host-Regeln übernehmen**. Until then, the edits are a draft. **Bestätigte Regeln** shows the rules currently shared with guests, including bots and the defeat condition. Applying a changed rule resets human readiness so everyone can review it. Changing a value to its already applied value does not reset readiness.

| Setting | Choices and meaning |
|---|---|
| **Erfahrungspunkte** | **Persönliche XP** is the default: each player collects for their own progression. **Team-XP** gives each connected participant the full amount from one accepted collection, rather than dividing it. |
| **XP-Drop-Positionen** | **Gleiche Positionen** is the default. With personal XP, players consume the same drop independently. **Persönliche Positionen** is available only with personal XP. Team XP forces synchronized positions and one team-wide collection. |
| **Pickups** | **Persönlich** is the default. Team sharing copies eligible coins and consumable rewards. Weapons, other inventory items, artifacts and unlocks remain personal. |
| **Bots / Bot-Schwierigkeit** | Reserve up to three of the four total lobby slots for bots; choose easy, normal or hard. Bots are not yet running in the game. |
| **Niederlage** | Choose defeat when all players are dead or when the first player dies. Spectating and game death handling are not connected yet. |

Shared enemy positions and health, and personal upgrade choices, are fixed requirements. They are not switches for separate combat worlds or a common upgrade selection. The recovery fields in the developer policy contract reserve a future reconnect policy; the application has no working run reconnect or lossless-save guarantee.

## Connection and delivery limits

Everyone must use the same application revision; lobby and gameplay transport each use protocol version 4 with separate version checks and reject incompatible clients. Use a trusted LAN or secured VPN; the transports are not encrypted.

The lobby client maintains its admission in the background even when no menu or roster is being refreshed. This does not restore an expired session and cannot keep a crashed or suspended process alive. **Leave** deliberately ends admission; opening a personal menu must not be implemented as leaving the lobby.

The TCP session library rejects conflicting reward replays and keeps a team event's original recipients. Repeating that event cannot grant it to someone who joined later. A successful publish means a receipt was queued, not that it was acknowledged or saved. Disconnects and process exits can still lose pending rewards; do not use this development build for valuable run progress.

See [connection help](CONNECTION_HELP.md), the [session API](../src/Wsm.Core/README.md) and the [developer guide](DEVELOPMENT.md) for diagnostics and verification commands.
