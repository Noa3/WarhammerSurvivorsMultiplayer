#include "wsm/portrait_layout.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
// P: live typed panel with a visible icon; I: inactive; D: unrelated decoration.
bool selection(std::string_view children, std::string_view expected) {
    std::string resized(children.size(), '-');
    wsm::layout_stock_portraits(static_cast<int>(children.size()), [&](int i) -> std::optional<int> {
        if (children[i] == 'P') return i;
        return {};
    }, [&](int i) { resized[i] = 'X'; });
    std::printf("%.*s: resized %s (expected %.*s)\n", static_cast<int>(children.size()), children.data(),
        resized.c_str(), static_cast<int>(expected.size()), expected.data());
    return resized == expected;
}

bool fault_isolation() {
    int reports = 0, status_reads = 0, inputs = 0;
    bool ui_error = false;
    for (int tick = 0; tick < 2; ++tick) {
        try {
            wsm::optional_portrait_update([&] {
                if (tick == 0) throw std::runtime_error("stale HUD");
            }, [&](const std::exception& e) { if (std::string_view(e.what()) == "stale HUD") ++reports; });
            if (!ui_error) ++status_reads;
            ++inputs;
        } catch (const std::exception&) { ui_error = true; }
    }
    std::printf("fault then retry: reports=%d status=%d input=%d UiError=%d\n", reports, status_reads, inputs, ui_error);
    return reports == 1 && status_reads == 2 && inputs == 2 && !ui_error;
}
}

int main() {
    // Stock upper-left VerticalLayoutGroup: top padding -50, spacing 10,
    // childControlHeight=false. Position advances by sizeDelta.y + spacing.
    auto tops = [](float height) { struct Tops { float first, second; }; float top = 50.f;
        return Tops{top, top - height - 10.f}; };
    auto before = tops(0), after = tops(wsm::portrait_slot_height(0, 100));
    auto gap = after.first - 100 - after.second;
    std::printf("slot0 %.0f -> %.0f; slot1 %.0f -> %.0f; portrait gap %.0f\n",
        before.first, after.first, before.second, after.second, gap);
    bool ok = before.first == after.first && after.second < before.second && gap == 10 &&
        wsm::portrait_slot_height(100, 100) == 100 && wsm::portrait_slot_height(140, 100) == 140;
    ok &= selection("PDI", "---");
    ok &= selection("DDPIPD", "--X-X-");
    ok &= selection("PPPP", "XXXX");
    ok &= selection("PPPPP", "-----");
    int visits = 0;
    wsm::layout_stock_portraits(1000000, [&](int) -> std::optional<int> { ++visits; return {}; }, [](int) {});
    ok &= visits == 0;
    ok &= fault_isolation();
    return !ok;
}
