#pragma once
#include "wsm/il2cpp_api.h"

namespace wsm {
// Developer-only, explicitly state-changing experiment. The operator must
// establish an outside-run phase before every mutation command. An active
// captured run or a failed player read is additionally refused, but NotReady
// alone is not an independently proven outside-run lifecycle signal.
// After menu-player selection, restore the original player before rollback;
// the roster API refuses removal while a different player remains selected.
// Owns/detaches the metadata worker attachment, including startup failures.
void run_coop_roster_probe(HMODULE bridge, HMODULE game, Il2CppApi api,
                           Il2CppDomain* domain, Il2CppThread* inspection_thread,
                           void (*log)(const std::string&));
}
