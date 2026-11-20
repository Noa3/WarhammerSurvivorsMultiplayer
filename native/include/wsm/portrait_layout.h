#pragma once
#include <array>
#include <exception>
#include <optional>

namespace wsm {
// The outer VerticalLayoutGroup reads the panel's own height, not its fitted grandchild.
inline float portrait_slot_height(float current, float icon) noexcept {
    return current < icon ? icon : current;
}

template<class Inspect, class Resize>
void layout_stock_portraits(int children, Inspect inspect, Resize resize) {
    // A larger/unknown hierarchy is not safe to classify from a partial scan.
    constexpr int child_scan_limit = 32;
    if (children < 2 || children > child_scan_limit) return;
    std::array<typename decltype(inspect(0))::value_type, 4> panels{};
    std::size_t count = 0;
    for (int i = 0; i < children; ++i) {
        if (auto portrait = inspect(i)) {
            if (count == panels.size()) return;
            panels[count++] = *portrait;
        }
    }
    if (count < 2) return;
    for (std::size_t i = 0; i < count; ++i) resize(panels[i]);
}

template<class Update, class Report>
void optional_portrait_update(Update update, Report report) {
    try { update(); }
    catch (const std::exception& e) { report(e); }
}
}
