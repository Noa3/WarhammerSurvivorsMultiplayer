#include "wsm/upgrade_choice_controller.h"
#include <cstdio>
#include <string>

int main() {
    wsm::UpgradeChoiceController controller;
    std::string error;
    wsm::UpgradeChoiceOffer offer;
    offer.token = 27;
    offer.input_id = 41;
    if (controller.capture(41, offer, error) != wsm::PlayerReadStatus::Error || error.empty() ||
        offer.token != 27 || offer.input_id != 41) {
        std::fputs("FAIL uninitialized capture refuses atomically\n", stderr);
        return 1;
    }
    error.clear();
    wsm::UpgradeChoiceResult result;
    if (controller.select(27, 41, 0, 7001, result, error) || error.empty() || result.invocation_attempted) {
        std::fputs("FAIL uninitialized selection never invokes a handler\n", stderr);
        return 1;
    }
    std::puts("PASS upgrade choice uninitialized guards");
    return 0;
}
