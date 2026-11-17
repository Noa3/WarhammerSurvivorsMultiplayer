#pragma once
#include "wsm/il2cpp_api.h"

namespace wsm {
// Developer-only, explicitly state-changing experiment. The operator must
// establish an outside-run phase before roster mutations. An active
// captured run or a failed player read is additionally refused there, but NotReady
// alone is not an independently proven outside-run lifecycle signal.
// After menu-player selection, restore the original player before rollback;
// the roster API refuses removal while a different player remains selected.
// The separately opted-in zero-only input trial instead requires a verified
// running scene with two living actors; it admits no input leases or nonzero axes.
// Its optional explicit source-definition ID is freshly validated, never inferred
// from a runtime controller ID. controlSources is a separate read-only diagnostic.
// Owns/detaches the metadata worker attachment, including startup failures.
void run_coop_roster_probe(HMODULE bridge, HMODULE game, Il2CppApi api,
                           Il2CppDomain* domain, Il2CppThread* inspection_thread,
                           void (*log)(const std::string&));
}
