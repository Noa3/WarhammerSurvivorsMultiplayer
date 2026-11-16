#pragma once
#include "wsm/player_state_reader.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wsm {
struct UpgradeChoiceCard {
    std::int32_t child_index = -1, weapon_type = 0, current_level = 0;
    bool is_new = false, interactable = false;
};
struct UpgradeChoiceOffer {
    std::uint64_t token = 0;
    std::int32_t input_id = -1, actor_level = 0;
    std::vector<UpgradeChoiceCard> cards;
};
struct UpgradeChoiceResult {
    bool invocation_attempted = false, invocation_returned = false;
};
// Local engine input IDs are not authenticated network identities. The caller
// must authorize the participant before requesting or selecting an offer.
// Captures do not call selection handlers. Selection invokes the ordinary
// public card OnClick only, never direct inventory writes or unlock APIs.
class UpgradeChoiceController {
public:
    UpgradeChoiceController();
    ~UpgradeChoiceController();
    UpgradeChoiceController(const UpgradeChoiceController&) = delete;
    UpgradeChoiceController& operator=(const UpgradeChoiceController&) = delete;
    bool initialize(HMODULE module, Il2CppApi api, Il2CppDomain* domain, DWORD native_thread,
                    std::int32_t managed_thread, std::string& error);
    // One outstanding offer; a successful new capture invalidates the old one.
    // Error/NotReady must not modify the caller's output. Token is process-local.
    [[nodiscard]] PlayerReadStatus capture(std::int32_t expected_input_id,
                                          UpgradeChoiceOffer& output, std::string& error);
    // Revalidates retained page/actor/container/card/data identity and liveness.
    // Consume before invocation even on handler exception; never retry silently.
    // Returned means callback completed, not independently observed gameplay.
    [[nodiscard]] bool select(std::uint64_t token, std::int32_t expected_input_id,
                              std::int32_t child_index, std::int32_t expected_weapon_type,
                              UpgradeChoiceResult& result, std::string& error);
    // Release managed roots on the verified main thread. Failed close retains
    // ownership; a destructor must not call IL2CPP from an arbitrary worker.
    bool close(std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
