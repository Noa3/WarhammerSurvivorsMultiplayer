# Player names

The co-op panel shows your local display name. A compact list in the lower-left corner remains visible when you hide the panel and shows the connected participants, with the host first. It is a connection roster, not a character-selection screen or a set of labels attached to remote heroes. Remote gameplay actors and their ownership mapping are still separate unfinished work.

## Steam names and fallbacks

The game uses the current Steam persona name when its existing Steam session is ready. This reads the game's initialized Steamworks wrapper; the mod does not sign you in, initialize another Steam session or change your Steam profile. If a name is unavailable, the local preference falls back to the interface's translated word for “Player.”

The local name source is refreshed periodically and immediately before hosting or joining. A connected session keeps the name captured at admission. Changing your Steam name during a connection affects the next connection, not the identity already displayed to other participants. Leave and join again to use the updated name.

Names keep their original language. Interface labels follow the game's language. Long names are shortened with an ellipsis to fit the display; this does not shorten the name transmitted to peers beyond the transport's normal limits. Glyph availability depends on installed fonts.

## Privacy and trust

Only the display name is used. The feature does not request Steam IDs, a friends list, account credentials or Steam authentication tickets. Names are self-declared presentation data over the existing trusted-LAN/VPN connection, **not proof of a Steam account**. Another participant can choose the same name. Control and session ownership use host-assigned identifiers, never name matching.

Control characters and line separators are removed, surrounding whitespace is trimmed, and names are limited to 48 Unicode scalar values and 192 UTF-8 bytes. Empty names become “Player.” Names are rendered as literal text: markup such as `<b>Captain</b>` cannot change the interface's formatting. Departed participants are removed from the roster; a lost connection clears remote names.

## Compatibility

All participants need a matching build. The name-aware session protocol is version **5**; older protocol-4 previews cannot join it. The original native connection ABI remains version 1, with additive optional name exports documented in the [native ABI header](../src/Wsm.InGame.Runtime/native/wsm_ingame_abi.h).

For launch, save-data warnings and connection setup, use the [Windows package guide](../distribution/README.md). Names do not enable shared gameplay, bots or progress-preserving reconnection.
