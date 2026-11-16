# Native bridge probes

Build and test the current source on Windows x64:

```text
cmake -S native -B artifacts/native-build -A x64
cmake --build artifacts/native-build --config Release
ctest --test-dir artifacts/native-build -C Release --output-on-failure
```

## Experimental player adapters

The source includes secondary-controller resource management, bounded remote-input leases and an active-owner upgrade-choice controller. These are developer components, not a connected gameplay adapter. The ordinary connection preview does not enable their diagnostic mutation routes.

The input library is tested against original Unity fixtures for controller ownership, neutralization and cleanup. Independent movement and combat attribution in the target game are still unverified. The upgrade library captures a bounded card offer and revalidates its owner, scene, item and one-shot token before invoking a selection callback. Invalid weapon types and stale or disabled choices are refused. Fixture callback effects do not establish compatibility with the target game's upgrade handler, repair its normal UI routing or provide separate native XP.

Diagnostic nonzero input pulses are unavailable until a persistent neutralization and cleanup-retry owner exists. Runtime `UpgradePassHook.install` and `remove` also refuse mutation because a stopped run does not prove that other threads are outside the patched code. Neutral controller inspection/open/close, low-level fixture tests and the separate owned-startup debugger remain available. Neither a command deadline nor a lease guarantees neutralization while Unity cannot execute its callback.

The managed gameplay ABI carries validated session messages, including recipient-private progression receipts. Native-ABI and TCP tests cover that transport boundary; they do not apply those messages to game objects or synchronize enemies. The original `tests/UnityPlayerStateFixture` sources cover the IL2CPP adapters separately from the normal repository verifier and require a compatible Unity editor to build. Do not redistribute installed game files as fixture dependencies.

Adapters must poll regularly. Core's separate 128-entry reward notification queue can stop the receiving session when full. The adapter reconciles receipts already accepted into Core's bounded ledger, but this does not guarantee continued connectivity or delivery of rewards Core has not accepted. The [gameplay ABI header](../src/Wsm.InGame.Runtime/native/wsm_ingame_abi.h) specifies the event kinds, request correlation and queue limits.

## Experimental camera following

The current source defaults to following the local primary actor, engine input 0. The localized in-game panel can request an own-player view or a shared view of living actors. Choosing a mode also retries a previously unavailable camera; enabled preference buttons do not mean the camera is attached. The status reports effective attachment and focus. This is not a network-client identity mapping or local split-screen implementation.

Shared following excludes dead actors. After local death, own-player mode follows a stable living spectator target and returns to the local actor after revival. The adapter changes only the aggregate target's X/Y position through `GameManager.ManualCameraTargetControl`; it does not change depth, zoom or bounds. A sole survivor's focus is therefore not a guarantee of pixel-identical stock single-player framing.

The [camera controller](include/wsm/player_camera_controller.h) validates current scene ownership even when an old scene stops invoking its callback. Cleanup retains ownership until confirmed and preserves foreign replacements. Callback addresses are not reused: the process supports at most 256 camera attachment generations, then refuses further attachment until restart. The [original Unity fixture](../tests/UnityPlayerStateFixture/Assets/CameraFixture.cs) exercises native callbacks, Transform effects and lifecycle failures. Own/shared focus and actual in-game mode controls have also been tested with game 2.1.6. A last-applied survivor target is not a fresh live survivor observation; exact death-transition framing and other game versions remain separate acceptance gates. Rebuild or update the complete portable package when using source changes.

## Scene probes

The bridge needs `WSM_GAME_LOG` to point to the current game's Unity log and `WSM_BRIDGE_LOG` to a writable bridge log. It waits for the game's managed version startup line before touching IL2CPP. Metadata-only inspection remains the default. `inspection thread detached` is its completion receipt. Never use a stale game log as proof that this process has started scripting.

Set `WSM_MAIN_THREAD_PROBE=1` to request one active-scene observation. The bridge finds a top-level `UnityWndClass` belonging to its own process and schedules the callback using a registered Windows message. The scheduler checks the target HWND, a per-invocation nonce and `PM_REMOVE`; ordinary messages cannot trigger it. Calls can be repeated, while overlapping calls fail. Acquisition has a 45-second deadline. A callback that has already started must finish before its caller returns and cannot safely be interrupted. C++ callback exceptions produce a failed result. The scheduler pins its actual containing module for the process lifetime, so return from scheduling is not permission to unload its code.

Before enumerating Unity objects, the callback independently checks the native Windows thread, the actual `UnitySynchronizationContext`, its `MainThreadId`, and `Thread.CurrentThread.ManagedThreadId`. Missing or ambiguous metadata, an unattached thread, a substituted context or mismatched identities stop observation. Window ownership alone is insufficient; `WSM_UNITY_THREAD_VERIFIED` records the successful combined check.

Optionally set `WSM_OBSERVATION_SIGNAL` to a fresh absolute file path before launching the controlled process. The worker emits `WSM_OBSERVATION_WAITING` and waits up to 15 minutes before scheduling the observation. Create that regular, non-linked file only when the intended scene is ready. Its contents are not read or executed. `WSM_OBSERVATION_REQUESTED` confirms receipt. An inaccessible path, directory, linked file or expired deadline is an error. Without this variable the observation starts immediately after metadata inspection. The current bridge still takes only one sample per process; a reusable scheduler is not a continuous gameplay adapter. Combining the deferred observation signal with local co-op activation is rejected before runtime inspection; the signal cannot authorize a game-state mutation.

`WSM_MAIN_THREAD_COMPLETE managers=N characters=N coop_buttons=N` is emitted only after all three `UnityEngine.Object.FindObjectsOfType(System.Type)` calls succeed on that callback and the button results pass `Behaviour.isActiveAndEnabled`. These are scene object counts, not prefab counts; `characters` includes all CharacterController instances on active scene objects, not specifically main players, and `coop_buttons` excludes disabled behaviours. Zero means absent at this instant. Enumeration success is not proof of gameplay, player identity or multiplayer. Timing matters: a menu probe does not inspect a later game scene.

Read-only enumeration has been verified in a user-operated, paused 40K run: the native and managed main-thread checks passed, with one GameManager and one CharacterController, and the game remained running. This establishes the observation entry point, not player identity, movement, XP ownership, networking, another campaign, an in-game menu or playable co-op. The original Unity fixture separately checks the production thread guard and negative controls; its preserved metadata does not establish compatibility with other game builds.

Set `WSM_ENABLE_LOCAL_COOP=1` *in addition* to `WSM_MAIN_THREAD_PROBE=1` to request the private instance `EnterCoopButton.EnterCoop()` once. The bridge only invokes it when precisely one active button is enumerated and its exact non-static `System.Void EnterCoop()` signature is present. `WSM_COOP_REQUESTED` means this call returned without a managed exception, **not** that coop is playable; `WSM_COOP_UNAVAILABLE` means no unique active instance or exact method. No object or prefab is created. Invoking this game method may change game state; it is separate from read-only observation and remains unverified. Prefer a separate test profile; do not run against personal progress without explicit authorization and a suitable backup.

`ERROR:` marks missing metadata/exports, a hook or acquisition timeout, unattached callback, null enumeration result, or a managed exception (exception class is logged). Missing startup is also an error. The native library is pinned for the lifetime of the process. See the [game-probe save precautions](../docs/DEVELOPMENT.md#game-probes-and-save-data) before starting it. Installation copies do not isolate saves or Steam Cloud. Co-op activation still requires independent live verification. Source improvements are not automatically present in older downloadable preview binaries.
