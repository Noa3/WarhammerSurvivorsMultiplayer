#pragma once
#include "wsm/il2cpp_api.h"
namespace wsm {
// Called by the attached inspection worker; retains only process-lifetime metadata.
void run_main_probe(HMODULE bridge, HMODULE game, Il2CppApi api, Il2CppDomain* domain,
                    bool coop, void (*log)(const std::string&));
}
