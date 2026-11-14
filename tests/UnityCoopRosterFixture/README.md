# Experimental roster fixture

This is an original developer fixture, not the game's code and not evidence
of game compatibility or co-op support. Its managed contracts model configured
players and a roster with explicit add-before, add-after, and remove fault paths.
`RemovePlayer` is deliberately private; `link.xml` preserves it.

The native driver links the implemented `native/src/coop_roster.cpp` rather than
copying roster logic. Its 20 named cases have executed successfully in a Unity
6000.0.74f1 non-development Windows x64 IL2CPP player. The original 13 cases cover
typed inspection, unknown/already-enrolled refusal, enrollment, repeat refusal,
exact rollback, wrong OS/managed identity, missing synchronization context,
exceptions before/after addition, removal failure/retry, and manager replacement.
Three menu-selection cases cover an unknown target, target ID 41 at index 1,
and restoration of the original player. Direct rollback while the target remains
selected must fail and retain ownership. Four character-copy cases cover an
unknown target, independent original/target selections and an unavailable source
character. The original managed contracts use synthetic character enums, not
proprietary game assets.

Isolated no-op-AddPlayer and no-op-character-write counterfactuals fail their
intended state assertions. Restoring the original plugin passes the complete
cohort for that version again. These are retrospective sensitivity checks,
not test-first behavior failures.

These cases are a bounded subset, not exhaustive coverage of every malformed
metadata, allocation, lifetime, out-of-band roster or reentrancy path. Roster
membership is not a spawned character. The adapter requires a caller-established
outside-run phase, serialized calls and a lifetime within the engine's runtime.
Pending additions must be explicitly rolled back; destruction retains their
roots until process exit instead of calling the engine from an unknown thread.

Use an independent project copy and output folder for each Unity build;
`Assets/Editor/Build.cs` selects a non-development Windows IL2CPP build and reads
`WSM_FIXTURE_OUTPUT`. Never treat a successful fixture as proof of game behavior,
save/event rollback, actor creation, or official co-op support.
