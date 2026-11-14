# Windows x64 distribution

## Status and installation

This package is **experimental lobby and diagnostic software, not playable multiplayer**. The separate lobby can communicate with another lobby; it does not provide in-game multiplayer or world replication. The game is not included. The self-contained Windows x64 .NET runtime is bundled, so the managed applications need no separate .NET SDK or runtime. A fresh-machine native runtime setup for the diagnostic bridge has not been certified.

* **Portable ZIP:** extract its top-level `WSM` folder to a clean location, retaining the directory structure. Start `WSM/lobby/Wsm.App.exe`. Do not overlay an existing folder. To uninstall, back up any files you added, then remove only the extracted `WSM` folder.
* **Vortex ZIP:** install with Vortex and a compatible Warhammer Survivors game extension. Its FOMOD maps `payload` to `WSM` relative to the extension's deployment root, intended to be the game folder. Check Vortex's actual deployment path before enabling. Remove the mod through Vortex; do not manually remove game files or files belonging to other mods. This archive's FOMOD schema and mapping have been checked, but installation and uninstall in Vortex with this game extension have **not** been tested; do not present them as certified.

[ChemBoy1's Warhammer Survivors Vortex Extension](https://www.nexusmods.com/site/mods/2584) hands FOMOD archives to Vortex's built-in FOMOD installer. Obtain the extension separately; it is not included here and must not be reuploaded without permission. Its installed 1.0.0 setup offers an unrelated loader installation when neither supported loader is present. **Do not install a loader just for WSM**; use the portable package if this prompt cannot be dismissed. The extension also excludes README and license-named files from deployment: keep the complete original ZIP and its notices. A dedicated FOMOD avoids the generic DLL-plugin installer, but does not bypass the extension's setup rules. A complete deployment/purge test without those unwanted dependencies is still required; this ZIP is a candidate, not certified Vortex support.

The bundled runtime license files and notices are under `WSM/licenses` after portable extraction and under `payload/licenses` in the Vortex ZIP. The installed Vortex extension may omit them from deployment, so retain the archive. The game and its prerequisites must be obtained separately. The manifest is an integrity inventory, **not a signature**; obtain archives and expected checksums through a trusted channel. Executables are unsigned and may require security review; do not disable antivirus to force installation.

Optional game launch stores copies/logs separately under `%LOCALAPPDATA%/WSM`, not inside the ZIP extraction or Vortex staging. Uninstalling the portable folder or Vortex mod does not remove those copies. After closing WSM and its launched game, review/back up that WSM folder before optionally removing it to reclaim disk space. Do not remove the game's own save folders or Steam Cloud data. See [game launch and save precautions](INGAME_MENUS.md#optional-single-player-launch).

## Create archives from an existing package

On Windows x64, with PowerShell 7, from the repository root:

```powershell
pwsh -NoProfile -File tools/archive.ps1 -InputDirectory distribution/win-x64 -OutputDirectory .local/archive-new
```

The input must be an already built and independently verified `win-x64` package; this command does not build the software or require Unity or a .NET SDK. The output's parent must already exist, and the output directory must not exist. The two archives are alternatives, not nested archives. Never overlay outputs or install both copies into the same `WSM` directory. The script verifies the manifest's complete file set, case, sizes and SHA-256, checks input and ancestor reparse points and rejects unsafe names and known private, debug, proprietary and archive paths. It rechecks each archived file against the manifest. It refuses an existing output and cleans up only its own partial outputs if creation fails.

The manifest and name rules cannot prove the provenance, licensing or contents of an arbitrary binary: an untrusted manifest can match untrusted files. Use a trusted package and review its contents and licenses before distributing it. The provided package records `playableMultiplayer: false` and has its resolved .NET runtime pack licenses. A matching digest does not establish authenticity. Reassess the license bundle for every rebuilt input.

### Validation

```powershell
python -m unittest discover -s tests/Packaging -v
python tests/Packaging/verify_release.py distribution/win-x64 .local/archive-new
```

Fixtures in the unit tests are arbitrary bytes and do not test runtime compatibility. `verify_release.py` extracts both archives into a temporary directory, checks CRC, exact ZIP entry sets, every extracted package file against the manifest, and the modeled FOMOD source-to-destination mapping. Set `WSM_TEST_SCRATCH` to a suitable writable temporary directory when desired.

For independent FOMOD 5.0 schema validation, download the two official schemas to a temporary directory, then run:

```powershell
Invoke-WebRequest https://qconsulting.ca/fo3/ModConfig5.0.xsd -OutFile "$env:TEMP/fo3-ModConfig5.0.xsd"
Invoke-WebRequest https://qconsulting.ca/gemm/ModConfig5.0.xsd -OutFile "$env:TEMP/gemm-ModConfig5.0.xsd"
pwsh -NoProfile -File tests/Packaging/validate_fomod.ps1 -Archive .local/archive-new/WSM-vortex-win-x64.zip -Schema "$env:TEMP/fo3-ModConfig5.0.xsd" -BaseSchema "$env:TEMP/gemm-ModConfig5.0.xsd"
```

The FO3 schema redefines the GEMM base; the validator redirects only that reference to the supplied local file. Compare downloaded schemas against trusted known versions before using them. XML schema and modeled mapping checks do not substitute for an actual Vortex installation/deployment/uninstall test. The [Vortex installer guide](https://github.com/Nexus-Mods/Vortex/wiki/MODDINGWIKI-Users-General-How-to-create-mod-installers) states destinations are relative to each game's mod folder, making the extension's actual deployment root significant.

## Publishing checklist

* Verify all components and third-party notices are redistributable and comply with the game's applicable terms. Do not include the game executable, UnityPlayer, GameAssembly, game metadata, generated game interop, user data, credentials, logs or private working files. Do not redistribute the Vortex extension.
* Describe the archives accurately as experimental lobby/diagnostic tools, not a playable multiplayer mod. Verify their actual functionality on a clean supported Windows machine and their Vortex behavior before making compatibility claims. Nexus prohibits public placeholders and requires functional files and accurate descriptions; its [file submission guidelines](https://help.nexusmods.com/article/28-file-submission-guidelines) require permissions and appropriate classification/tags, including applicable generative-AI disclosure. For a Nexus upload of the current codebase, the applicable classification is **AI-Generated Content**, because most implementation code was generated with AI. This disclosure does not require publishing private working documents. Review the current [AI policy](https://help.nexusmods.com/article/180-ai-policy-changes-2026) and permission fields before uploading; no platform approval is implied.
* Nexus says network-connected executables are restricted unless communication is crucial, and asks authors whose tools depend on sending/receiving information to contact staff and provide source code. The lobby communicates over the network; consult the [submission guidelines](https://help.nexusmods.com/article/28-file-submission-guidelines) and Nexus staff before publication. Files undergo security scans and may be quarantined. Nexus [recommends ZIP or 7z and rejects nested archives](https://help.nexusmods.com/article/117-why-has-my-mod-been-quarantined); these ZIPs are separate and unencrypted. No Nexus approval or upload is implied by producing them.
* Publish SHA-256 checksums through a trusted channel and retain exact package and runtime license provenance. GitHub Releases and Nexus may apply different rules; verify each platform's current requirements before uploading.
