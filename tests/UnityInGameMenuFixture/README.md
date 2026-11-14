# Original Unity in-game menu fixture

This is a standalone original Unity 6000.0.74f1 Windows IL2CPP fixture. It never loads, launches, or attaches to the game and does not test networking. Fixture callbacks only record invocation count and arguments. The Unity SDK player/build outputs remain local and are not redistributable. `link.xml` intentionally retains metadata: this fixture does not prove what survives the shipped game's stripping.

From the repository root in Git Bash with the matching Unity SDK and MSVC x64:

```bash
cmake -S tests/UnityInGameMenuFixture/Native -B .local/ingame-ui-v1-native -A x64
cmake --build .local/ingame-ui-v1-native --config Release
mkdir -p .local/ingame-ui-v1-work/Assets/Plugins/x86_64
cp -r tests/UnityInGameMenuFixture/. .local/ingame-ui-v1-work/
cp .local/ingame-ui-v1-native/Release/ingame_ui_fixture.dll .local/ingame-ui-v1-work/Assets/Plugins/x86_64/
export WSM_MENU_OUTPUT='E:/WarhammerSurvivorsMultiplayer/.local/ingame-ui-v1-player/Fixture.exe'
'C:/Program Files/Unity/Hub/Editor/6000.0.74f1/Editor/Unity.exe' -batchmode -nographics -quit -projectPath 'E:/WarhammerSurvivorsMultiplayer/.local/ingame-ui-v1-work' -executeMethod BuildFixture.Build -logFile 'E:/WarhammerSurvivorsMultiplayer/.local/ingame-ui-v1-build.log'
python tests/UnityInGameMenuFixture/run_fixture.py final-positive
python tests/UnityInGameMenuFixture/run_fixture.py forced-negative # expected exit 1
python tests/UnityInGameMenuFixture/run_os_input.py
```

Check the Unity build log for `MENU_FIXTURE_BUILD_RESULT=Succeeded`. The fixture runs windowed at 1280x720; `run_os_input.py` tries exactly one background-posted mouse click and confirms no callback, then may explicitly foreground **only its freshly launched fixture window** and uses `SendInput` for three physical clicks. It derives coordinates from the fixture's actual Unity `RectTransform` and client geometry, validates the foreground before each press, and stops if focus changes. This test changes focus and cursor position; run only when that is acceptable. It does not send input to the game or other programs. The other runner tests the same native focus/edge state machine with controlled samples and the real Unity `RectTransformUtility` hit test, real `EventSystem` raycasts, key events into the actual Unity `InputField`, callbacks, validation, and destruction. `--force-failure` is a harness negative control. Screenshots and logs use fresh run-specific paths under `.local/ingame-ui-v1-*`.

## Parent integration contract

Include `wsm/ingame_ui.h` and compile `native/src/ingame_ui.cpp` into the existing in-process native bridge. Populate `Il2CppApi` with both `resolve()` and `resolve_probe()`. After the existing guard has verified the Unity main thread, call `initialize(gameAssembly, api, domain, ownedUnityHwnd, error)`, then `tick(actions)` on that same thread repeatedly, and `shutdown()` there before bridge/runtime unload. The caller owns the callback context and must keep it alive until shutdown; callbacks must not synchronously destroy/reenter the menu. Null callbacks are safe and display an unavailable status. `status` is polled on each tick and should be cheap/nonblocking. The owner window must belong to the current process and current thread; this check supplements, not replaces, the production main-thread guard. No network session is implemented here.

The Unity UI uses a persistent screen-space `Canvas`, a `GraphicRaycaster`, native Unity `Button`/`InputField`/`Text`, and a blocking `Image` panel. It resolves exact method signatures at runtime and aborts on missing or ambiguous required metadata. The font comes from an already loaded Unity font or Unity 6000's built-in `LegacyRuntime.ttf`, the resource used in the SDK uGUI `Text.AssignDefaultFont` implementation. No font or proprietary game asset is shipped. Managed objects are retained by IL2CPP GC handles and the owned root is destroyed on shutdown. Windows mouse and F6 messages are buffered only from the owning foreground window; no global monitoring is performed. F6 and the Koop tab toggle the panel.

`run_os_input.py --quick-click-regression --edit-fields` sends short and held F6, drag cancellation, real field edits and button clicks to the owned player; the managed fixture checks the callback IP and port. `--modal` is a currently failing regression: it verifies the higher modal wins `EventSystem.RaycastAll`, then demonstrates the native geometric handler still invokes Host underneath. `--edit-fields --no-event-system` also currently fails the expected callback-value check. The sample-driven `fixture_input` checks exercise a legacy test seam, not the production Win32 queue.

Limitations to independently verify in the real game: the game's stripping and package versions may omit required legacy uGUI metadata; an existing and correctly configured Unity `EventSystem`/input module is required for editing fields; game-specific custom input, resolution/DPI, sorting layers, scene changes, and any raw gameplay mouse input outside Unity's raycast system can behave differently. A Canvas raycaster blocks underlying Unity UI but cannot guarantee that unrelated gameplay code ignores raw mouse input. The fixture does not validate the real game, networking, or public firewall behavior. No MelonLoader/proxy assembly is a dependency.
