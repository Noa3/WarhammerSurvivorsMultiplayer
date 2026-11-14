#pragma once
#include "wsm/il2cpp_api.h"
#include <memory>
#include <vector>

namespace wsm {
struct PlayerStateSnapshot {
    std::int32_t slot = 0, input_id = 0, character_type = 0, level = 0;
    float x = 0, y = 0, hp = 0, xp = 0;
    bool dead = false;
};
struct SceneStateSnapshot { std::int32_t stage_type = 0; std::vector<PlayerStateSnapshot> players; };
enum class PlayerReadStatus { Captured, NotReady, Error };
// Metadata is borrowed for the runtime lifetime. The expected IDs must be
// independently established by the caller; managed_thread may be zero.
class PlayerStateReader {
public:
    PlayerStateReader();
    ~PlayerStateReader();
    PlayerStateReader(const PlayerStateReader&) = delete;
    PlayerStateReader& operator=(const PlayerStateReader&) = delete;
    bool initialize(HMODULE module, Il2CppApi api, Il2CppDomain* domain, DWORD native_thread,
                    std::int32_t managed_thread, std::string& error);
    [[nodiscard]] PlayerReadStatus read(SceneStateSnapshot& output, std::string& error) const;
#ifdef WSM_PLAYER_STATE_TESTING
    void test_fail_next_scene_root(bool players);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
