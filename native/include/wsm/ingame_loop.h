#pragma once
#include "wsm/il2cpp_api.h"

namespace wsm {
// Called by the attached inspection worker, never directly from DllMain.
// All Unity work is dispatched through a separately verified main-thread callback.
void run_ingame_menu(HMODULE bridge, HMODULE game, Il2CppApi api, Il2CppDomain* domain, Il2CppThread* inspection_thread,
                     void (*log)(const std::string&));
}
