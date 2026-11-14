Warhammer Survivors Multiplayer - experimental tools for Windows x64

This is NOT playable multiplayer. It provides a separate lobby application,
diagnostic launcher and bridge; it does not implement gameplay multiplayer.
The Windows x64 .NET runtime is included. Keep this folder and its contents
together. The game is not included and must be obtained separately.

Portable: extract the WSM folder to a clean location and start
WSM\lobby\Wsm.App.exe. Do not overlay an existing folder. To remove it, delete
only the WSM folder you extracted, after backing up any files you added.

Vortex: this is a candidate FOMOD package, NOT certified Vortex support.
Install using a compatible game extension; the FOMOD maps payload into WSM
under its deployment root. Intended launch path after deployment:
<game-folder>\WSM\lobby\Wsm.App.exe. Verify the actual path first.
The installed Warhammer Survivors extension 1.0.0 offers unrelated loaders
when none is installed. Do NOT install a loader just for WSM; use the portable
ZIP if that prompt cannot be dismissed. The extension can omit README and
LICENSE-named files from deployment: retain the complete original ZIP and
notices in payload/licenses. Installation/deploy/purge have not been tested.
Uninstall using Vortex; do not delete game files or another mod's data.

Optional "Spiel separat starten (experimentell)" starts a copied installation,
not multiplayer. The lobby remains a separate window. It does NOT isolate
saves, user profile or Steam Cloud. Read and accept the warning before launch.
Copies/logs are stored in %LOCALAPPDATA%\WSM and survive mod removal. Close WSM
and its game before reviewing/backing up that folder for optional cleanup.
Never delete the game's own save folders or cloud data as part of uninstall.

The launcher/bridge are diagnostics and the bridge's native runtime
requirements on a clean machine are not certified. Runtime licenses are in
licenses/. The manifest lists file hashes; it is not an authenticity signature.
