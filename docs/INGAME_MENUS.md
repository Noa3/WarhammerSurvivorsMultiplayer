# In-game menus and connections

**The current connection preview adds a native Co-op [F6] panel inside the game.** Host, IP/port Join and Leave use a network runtime in the game process. The panel follows the selected game language. It does not start a shared multiplayer run.

Start `distribution/win-x64/Start-WSM.cmd` from a complete checkout. Enter your installed game folder, read the save warning and confirm. F6 hides/reopens the panel without leaving; Leave explicitly ends membership. Endpoints are IPv4 literals. Text editing is owned by the panel rather than vanilla menu selection; it supports select-all, cursor/selection movement, deletion and copy/paste.

In-game edited endpoints, Host/Join/Leave, separate real test peers and normal exit have been exercised on loopback. Two-computer LAN operation and gameplay input isolation remain separate acceptance gates. Do not assume opening this panel pauses combat or blocks every raw input consumer. See [interface language](LOCALIZATION.md) for font limits.

## Optional single-player launch

The legacy separate developer lobby retains **Spiel separat starten (experimentell)**. Select it, choose your installed game folder and read the confirmation before proceeding. The application prepares a full separate binary/data copy in `%LOCALAPPDATA%/WSM/runtime-cache` and starts it windowed. Allow additional disk space approximately equal to the copied installation; verification may take time. All included files are hashed so asset-only updates create a fresh cache. Old caches are retained, not silently deleted. Logs are stored in `%LOCALAPPDATA%/WSM/logs`.

**This does not isolate saves, the user profile or Steam Cloud.** The game may still read and modify its normal user data. The separate lobby stays visible and its connection continues. Close the application explicitly to close the child game it started; it never attaches to an already running game. The launch/lobby lifecycle is tested with an original Unity validation player, not a real multiplayer run or the installed game. Cross-process in-game surfaces are not shipped because their visible rendering was unreliable in the Unity test.

## Preview limits

A character queued immediately before switching focus away from the game can be discarded before the next UI tick; check the displayed endpoint after returning. This preview does not promise lossless editing across focus changes. Clipboard commands are implemented, but their OS-level paths have not received the same target-game coverage as direct typing and select-all.

Normal game shutdown with an open panel has been tested. Recovery from an exceptional Unity `Destroy` failure is not guaranteed; a failed teardown can leave the overlay until the owned game process exits. Close the game normally if this occurs. Do not interpret panel closure or a live connection as proof that raw game input, simulation or gameplay is isolated.

## Campaign selection and preparation

The intended entry is a co-op action after selecting a campaign, before starting a run. Hosting and joining belong there rather than replacing the original campaign selector. The host chooses shared campaign/run settings; each participant keeps their own hero, weapons, permanent upgrades and unlock availability. The application must validate these choices against the actual game before accepting a start.

Returning to equipment, upgrades, descriptions or the bestiary should keep the participant in the same party. The lobby should show that the player is still preparing. A change affecting the selected build clears that player's readiness; a shared rule change clears everyone's readiness. The host starts only after everyone has explicitly confirmed their current selection. Returning from a menu must not silently start a run or replace another player's build.

## During a run

Personal browsing and shared pause are separate actions. Opening a description or personal menu should block that player's gameplay input while allowing network processing to continue. It must not send a leave request, disconnect the participant, apply automatic invulnerability or silently pause the other players. The interface must warn the player if combat continues; menus that cannot safely operate during combat should remain preparation-only.

A shared pause should be clearly visible to everyone and require host confirmation. Level-up choices remain personal. The existing policy describes a coordinated upgrade pause: everyone sees why the run is paused and resumes after the required players finish their own choices. These gameplay/input/pause rules are not implemented yet. Opening a menu is not proof that Unity's simulation or the network worker has paused.

At run end, the party should return to its preparation lobby instead of requiring another connection. An explicit leave action ends membership. Host loss, reconnect, spectator controls and saved run progress need separate game integration; the current build does not promise recovery after a process crash or network outage.

## What the network currently provides

The HTTP lobby client sends background heartbeats independently of application window refreshes. Its server still expires a genuinely silent guest. The TCP session library sends a correlated ping every five seconds and requires its matching pong within 30 seconds. Its accept, receive and keepalive loops run independently of the UI/game synchronization context. Tests cover an idle session and a blocked menu context for more than 30 seconds, plus peers that send other traffic but do not answer pings. See the [session API](../src/Wsm.Core/README.md) for framing and timeout details. Neither transport can keep a crashed, sleeping or suspended process alive, and keeping a connection alive does not restore a lost run or save data.

The native menu performs Unity calls only through the verified main-thread window callback. The actual game has been used to check menu rendering, text input and network ownership. Original Unity 6000.0.74f1 IL2CPP fixtures independently cover interface behavior, foreign-selection interference and language changes; these checks do not establish shared-world gameplay. Their built Unity players are not distributed.

See [session rules](SESSION_RULES.md) for current host settings, or [connection help](CONNECTION_HELP.md) for failed connections.
