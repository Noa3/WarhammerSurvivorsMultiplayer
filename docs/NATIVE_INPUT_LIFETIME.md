# Native input ownership and recovery

This is developer infrastructure for independent player input. It does **not** make the connection preview a playable multiplayer mod. The target game's input-consumption order and an actual second player's movement still need separate verification. The existing `controlPulse` diagnostic remains refused, including when its old opt-in is set.

## Components

- `InputLease` validates remote player, session epoch, increasing sequence, finite bounded axes and a duration of at most 250 ms. Expiry changes the next sampled value; it cannot call the engine by itself.
- `InputDriver` owns a backend and calls it only from the event or recovery pump. Producers may submit, request stop and read its value snapshot without making engine calls. Recovery never applies nonzero input. Setter failures can have partial effects, so failed cleanup retains the backend and must be retried. A successful backend close must actually release the override. Destroying a driver with outstanding backend ownership terminates rather than silently discarding it.
- `RewiredInputPump` owns an `InputPumpClient` beyond the producing worker and wrapper. It subscribes a rooted `System.Action` to `ReInput.InputSourceUpdateEvent`. A separate native worker dispatches recovery through the owned Unity window; all client start, input and recovery operations must pass the existing native/managed Unity-main-thread guard. The worker does not call Unity directly.
- `RewiredInputClient` connects that pump and driver to `RewiredControllerSession`. The driver takes ownership of the backend before controller opening is attempted, so partial-open failure remains recoverable. A shared producer mailbox only submits leases, requests stop and reads driver snapshots. Nonzero event writes require the owned game's real foreground window and a running scene with a living remote actor. Independent recovery also checks run/actor lifetime and retires resources when that lifetime ends.

These boundaries are deliberately separate. The current production event observer does not own a controller or set axes. No target-game command enables the new controller client until input timing is verified. An engine-free driver test, a controlled pump client and a real controller-session composition in original managed doubles are separate levels of evidence; none establishes target-game movement or multiplayer synchronization.

## Shutdown and callbacks

A stop request is not synchronous cleanup. The pump retains its client and delegate while cleanup is outstanding, including an event add/remove that throws after taking effect. Removal must succeed and in-flight dispatch must drain before the delegate root is released. A retained old action must never route into a later owner; callback slots are not recycled during the process lifetime. The callback capacity is 256, including consumed registration attempts.

Foreign-thread and reentrant event callbacks are refused and request stop. Independent recovery can retry without another input event and after wrapper destruction. A lost window or stalled main thread still prevents timely engine work: neither a lease deadline nor a background worker guarantees immediate neutralization. An unresolved loss must remain visible, not be reported as successful cleanup.

## Original IL2CPP fixture

The fixture uses original public-contract event and client doubles, not the vendor's input implementation or game files. It executes real compiled IL2CPP callbacks, GC, worker destruction, independent recovery, foreign callbacks, reentry and ambiguous subscription failures against the production pump.

On Windows x64, install the Unity version in `tests/UnityPlayerStateFixture/ProjectSettings/ProjectVersion.txt` with Windows IL2CPP support, Visual Studio C++ build tools and CMake. From the repository root:

```powershell
python tests/UnityPlayerStateFixture/build_input_pump_fixture.py
```

The builder selects the corresponding standard Unity Hub editor path; use `--unity "C:/path/to/Unity.exe"` to override it. It creates a new local run directory, builds the native plugin and a fresh standalone Unity player, then runs every independently listed pump assertion. It does not require a cached private binary. `--output` accepts a new, nonexistent directory; existing runs are preserved. The printed `build-result.json` records commands, exits, source hashes and the actual player result. The run is accepted only with exact case membership, no failed assertions, a normal player exit and unchanged sources/player files.

To exercise an already built player with a known plugin digest, use `run_input_pump_fixture.py --player <directory> --plugin-sha256 <sha256>`. Its `--expect-contract-red` mode is only a negative check against a plugin missing the new interface, not a passing behavior suite.

The engine-free driver is registered in native CTest. Its separate pending-destruction check requires exact exit 42 and checks that the backend was not destroyed before termination:

```powershell
cmake -S native -B artifacts/native-build -A x64
cmake --build artifacts/native-build --config Release
ctest --test-dir artifacts/native-build -C Release --output-on-failure
cmake -DTEST_BINARY=C:/absolute/path/to/input_driver_test.exe -P tests/native/check_input_driver_lifetime.cmake
```

The actual pump/driver/controller-session composition has a separate suite:

```powershell
python tests/UnityPlayerStateFixture/build_input_pump_fixture.py --suite driver
```

This is an interactive Windows fixture, not a headless-focus substitute. It holds setup for up to 60 seconds so the exact owned fixture window can receive real foreground focus, then verifies that focus again at the native boundary. Keep it foreground during the short input checks. Its assertions inspect actual mapped remote-player axes, primary resources, lease expiry without more input events, failed neutralization/removal after producer destruction, and run/death cleanup. A focus-refused run is not an executed nonzero-axis test. Unneeded SDK firewall prompts can be canceled; opening network access is not required for these local callbacks.

## Bounded target observation

The existing owned-process roster diagnostic accepts PID-bound `inputWatchStart:<pid>`, `inputWatchState:<pid>` and `inputWatchStop:<pid>` commands. Subscription requires the separate exact `WSM_INPUT_PUMP_OBSERVATION_OPT_IN=1` and verified Unity thread/window. Status and stop are data-only operations. This is a developer diagnostic, not a public attach-to-arbitrary-process interface or multiplayer input transport.

The `inputEventWatch` result identifies whether a snapshot was captured, subscription/stop/cleanup/recovery state, refused callbacks and update counts/timestamps. UInt64 counters and timestamps are decimal strings to preserve their full values in JSON consumers. It explicitly reports `nonzeroEnabled: false` and `consumerOrderingProven: false`. Event delivery proves neither when `CharacterController` consumes axes nor independent movement/combat. Successful stop admission likewise does not mean `cleanupComplete` is already true.
