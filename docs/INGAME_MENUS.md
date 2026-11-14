# In-game menus and connections

**The current application has a separate Windows lobby. It does not add buttons to Warhammer Survivors yet.** Neither campaign selection nor bestiary/upgrade navigation is connected to the lobby. Do not expect the packaged application to show an in-game menu or start a multiplayer run.

## Campaign selection and preparation

The intended entry is a co-op action after selecting a campaign, before starting a run. Hosting and joining belong there rather than replacing the original campaign selector. The host chooses shared campaign/run settings; each participant keeps their own hero, weapons, permanent upgrades and unlock availability. The application must validate these choices against the actual game before accepting a start.

Returning to equipment, upgrades, descriptions or the bestiary should keep the participant in the same party. The lobby should show that the player is still preparing. A change affecting the selected build clears that player's readiness; a shared rule change clears everyone's readiness. The host starts only after everyone has explicitly confirmed their current selection. Returning from a menu must not silently start a run or replace another player's build.

## During a run

Personal browsing and shared pause are separate actions. Opening a description or personal menu should block that player's gameplay input while allowing network processing to continue. It must not send a leave request, disconnect the participant, apply automatic invulnerability or silently pause the other players. The interface must warn the player if combat continues; menus that cannot safely operate during combat should remain preparation-only.

A shared pause should be clearly visible to everyone and require host confirmation. Level-up choices remain personal. The existing policy describes a coordinated upgrade pause: everyone sees why the run is paused and resumes after the required players finish their own choices. These gameplay/input/pause rules are not implemented yet. Opening a menu is not proof that Unity's simulation or the network worker has paused.

At run end, the party should return to its preparation lobby instead of requiring another connection. An explicit leave action ends membership. Host loss, reconnect, spectator controls and saved run progress need separate game integration; the current build does not promise recovery after a process crash or network outage.

## What the network currently provides

The HTTP lobby client sends background heartbeats independently of application window refreshes. Its server still expires a genuinely silent guest. The TCP session library sends a correlated ping every five seconds and requires its matching pong within 30 seconds. Its accept, receive and keepalive loops run independently of the UI/game synchronization context. Tests cover an idle session and a blocked menu context for more than 30 seconds, plus peers that send other traffic but do not answer pings. See the [session API](../src/Wsm.Core/README.md) for framing and timeout details. Neither transport can keep a crashed, sleeping or suspended process alive, and keeping a connection alive does not restore a lost run or save data.

The native observation tools do not yet provide a verified safe gameplay-update/menu hook. Building an original IL2CPP validation player also requires Windows IL2CPP build support, which was absent from the tested local Unity editor. No incomplete fixture or speculative menu hook is part of the runtime package.

See [session rules](SESSION_RULES.md) for current host settings, or [connection help](CONNECTION_HELP.md) for failed connections.
