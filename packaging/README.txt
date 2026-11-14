Warhammer Survivors Multiplayer - in-game connection preview for Windows x64

This is NOT playable multiplayer. Host, Join and Leave are inside the game;
world replication, remote gameplay and shared runs are not implemented.
The Windows x64 .NET runtime is included. Keep the complete package together.
The native bridge also needs Microsoft Visual C++ x64 runtime libraries.
Official installer: https://aka.ms/vc14/vc_redist.x64.exe
The game and its normal Steam prerequisites must be obtained separately.

Portable: extract the complete WSM folder to a clean location, then run
WSM\Start-WSM.cmd. Enter your installed game folder and read the save warning.
Normal saves, profile data and Steam Cloud are NOT isolated. Back up valuable
saves before confirming. Open Co-op [F6] inside the launched game.

Use only a trusted LAN or secured VPN: network data is unencrypted and the
connection preview has no lobby password. Use IPv4 addresses; default port7780.
Only permit necessary private-network firewall access, not public exposure.
Cross-machine connectivity and gameplay input isolation are not certified.

The launcher creates verified installation copies and logs under
%LOCALAPPDATA%\WarhammerSurvivorsMultiplayer. These survive package removal.
Close its game before reviewing/backing up that folder for optional cleanup.
Never delete the game's saves or Steam Cloud data as part of uninstall.
Do not overlay an existing package. Remove only the folder you extracted.

Vortex: candidate FOMOD only, NOT certified install/deploy/purge support.
Its payload maps to WSM under the extension's deployment root. Check the
actual path first; the intended entry is <game-folder>\WSM\Start-WSM.cmd.
The installed game extension can offer unrelated loader installation and omit
README/license-named files from deployment. Use the portable alternative if
setup cannot proceed without unwanted changes, and retain all license notices.
Uninstall through Vortex; do not remove game files or other mods' data.

The separate lobby is retained only as a legacy developer tool. Its rule
controls do not configure a running game. Clean-machine native compatibility
has not been certified. Runtime notices are in licenses/. The checksum
manifest is an integrity inventory, not an authenticity signature.
