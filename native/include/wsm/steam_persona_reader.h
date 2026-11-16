#pragma once
#include "wsm/player_state_reader.h"
#include <memory>
#include <string>

namespace wsm {
// Optional, read-only access to the game's existing Steamworks wrapper. Failure
// to initialize this reader must not prevent unrelated in-game UI from starting.
class SteamPersonaReader {
public:
    SteamPersonaReader();
    ~SteamPersonaReader();
    SteamPersonaReader(const SteamPersonaReader&) = delete;
    SteamPersonaReader& operator=(const SteamPersonaReader&) = delete;
    [[nodiscard]] bool initialize(HMODULE module, Il2CppApi api, Il2CppDomain* domain,
                                  DWORD expected_os, std::int32_t expected_managed,
                                  std::string& error);
    // NotReady and Error leave output unchanged. No Steam lifecycle calls are made.
    [[nodiscard]] PlayerReadStatus read(std::string& output, std::string& error) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
