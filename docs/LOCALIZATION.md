# In-game interface language

The co-op panel follows the language selected **inside Warhammer Survivors**. It reads the game's current language code; it does not use the Windows display language or change the game's language settings.

Captions, connection states, validation messages and errors update when the game language changes. Entered IP addresses and ports remain unchanged, and changing language does not host, join, leave or restart a network session. If the language code is unavailable or unsupported, the panel uses English.

## Languages

English, French, Italian, German, Spanish, Japanese, Korean, Polish, Brazilian Portuguese, Russian, Simplified Chinese, Turkish and Ukrainian.

The panel uses Windows font families for its own text, including separate Japanese, Korean and Simplified Chinese families where the runtime exposes dynamic-font creation. Font files are not bundled. Missing Windows fonts or stripped font APIs can still result in missing non-Latin glyphs; the panel does not download or install fonts. Coverage on other Windows installations remains a compatibility limit. Text and font rendering in an original Unity test player are separate checks from compatibility with the installed game.

This applies to the native in-game panel in the current source, not the separate application in the older tools-preview release. It does not establish working multiplayer gameplay.

## Development

- `native/src/ingame_localization.cpp` contains the UTF-8 text catalogue and language-code normalization.
- `native/src/ingame_ui.cpp` reads `I2.Loc.LocalizationManager.CurrentLanguageCode` through exact IL2CPP metadata and refreshes the existing widgets on Unity's main thread.
- `native/src/ingame_runtime.cpp` formats connection states using stable machine-readable runtime exports. It does not translate or parse the backend's human-readable messages.
- UI object names remain stable internal identifiers; only displayed text is translated.

Native catalogue and status tests run through CTest. `tests/UnityInGameMenuFixture` additionally exercises live language changes, preserved input and font glyphs in an original Unity player. Its small localization assembly is an independently written test contract, not a redistribution of the game's localization code.
