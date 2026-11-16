#pragma once
#include "wsm/il2cpp_api.h"
#include <memory>
#include <string>

namespace wsm {
struct InGameMenuActions {
    void* context = nullptr;
    void (*host)(void*, int port) = nullptr;
    void (*join)(void*, const std::string& address, int port) = nullptr;
    void (*leave)(void*) = nullptr;
    std::string (*status)(void*, const std::string& language) = nullptr;
    void (*camera_mode)(void*, bool shared) = nullptr;
    std::string (*camera_status)(void*, const std::string& language) = nullptr;
};

// All operations, including destruction, must run on the verified Unity main thread.
class InGameMenu {
public:
    InGameMenu();
    ~InGameMenu();
    InGameMenu(const InGameMenu&) = delete;
    InGameMenu& operator=(const InGameMenu&) = delete;
    bool initialize(HMODULE game, const Il2CppApi& api, Il2CppDomain* domain,
                    HWND owner, std::string& error);
    void tick(const InGameMenuActions& actions);
    void set_visible(bool value);
    std::string language() const;
    void shutdown();
#ifdef WSM_INGAME_UI_TESTING
    void test_fail_next_language_label();
    // Test seam: executes the same Unity RectTransform hit path as a foreground click.
    bool test_click(float x, float y, const InGameMenuActions& actions);
    // Test seam for the very same focus/edge state machine used by tick().
    void test_input(bool foreground, bool mouse_down, bool f6_down, float x, float y, const InGameMenuActions& actions);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
