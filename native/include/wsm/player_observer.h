#pragma once
#include "wsm/player_state_reader.h"
#include "wsm/main_thread_guard.h"

namespace wsm {
// JSON contains a scene only after a complete, verified capture.
std::string player_observation_json(PlayerReadStatus status, const SceneStateSnapshot& scene,
                                    const MainThreadIdentity& identity, unsigned sample);
// Owns and detaches the supplied metadata-only thread attachment, including failures.
// Produces at most eight operator-requested observations without explicit gameplay
// setters or mutation APIs. Side effects of game-provided getters are unverified.
void run_player_observer(HMODULE bridge, HMODULE game, Il2CppApi api, Il2CppDomain* domain,
                         Il2CppThread* inspection_thread, void (*log)(const std::string&));
}
