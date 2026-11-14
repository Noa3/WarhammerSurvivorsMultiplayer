# Native bridge probes

Build and test on Windows x64:

```text
cmake -S native -B artifacts/native-build -A x64
cmake --build artifacts/native-build --config Release
ctest --test-dir artifacts/native-build -C Release --output-on-failure
```

The bridge needs `WSM_GAME_LOG` to point to the current game's Unity log and `WSM_BRIDGE_LOG` to a writable bridge log. It waits for the game's managed version startup line before touching IL2CPP. Metadata-only inspection remains the default. `inspection thread detached` is its completion receipt. Never use a stale game log as proof that this process has started scripting.

Set `WSM_MAIN_THREAD_PROBE=1` to opt in to a single window-message hook. After metadata inspection, the bridge looks for an unowned top-level `UnityWndClass` belonging to the current process. It hooks its owner thread, sends `WM_NULL`, invokes exact metadata-validated Unity enumeration methods from the callback and chains the hook. Hook/window acquisition has a 45-second deadline; an already executing managed callback cannot safely be interrupted and may exceed it. `WSM_WINDOW_FOUND thread=...` and `WSM_WINDOW_CALLBACK thread=...` identify the actual thread, but window ownership alone does not prove Unity engine-main-thread identity. A non-attached callback thread is rejected.

`WSM_MAIN_THREAD_COMPLETE managers=N characters=N coop_buttons=N` is emitted only after all three `UnityEngine.Object.FindObjectsOfType(System.Type)` calls succeed on that callback and the button results pass `Behaviour.isActiveAndEnabled`. These are scene object counts, not prefab counts; `characters` includes all CharacterController instances on active scene objects, not specifically main players, and `coop_buttons` excludes disabled behaviours. Zero means absent at this instant. Enumeration success is not proof of gameplay, player identity or multiplayer. Timing matters: a menu probe does not inspect a later game scene.

Set `WSM_ENABLE_LOCAL_COOP=1` *in addition* to `WSM_MAIN_THREAD_PROBE=1` to request the private instance `EnterCoopButton.EnterCoop()` once. The bridge only invokes it when precisely one active button is enumerated and its exact non-static `System.Void EnterCoop()` signature is present. `WSM_COOP_REQUESTED` means this call returned without a managed exception, **not** that coop is playable; `WSM_COOP_UNAVAILABLE` means no unique active instance or exact method. No object or prefab is created. Invoking this game method may change game state; enable only in an appropriately isolated game profile.

`ERROR:` marks missing metadata/exports, a hook or acquisition timeout, unattached callback, null enumeration result, or a managed exception (exception class is logged). Missing startup is also an error. The native library is pinned for process lifetime; no MethodInfo memory layout, inline patches, third-party hooking library, or exported main-thread tick is used. In-game enumeration and coop activation require independent live verification.
