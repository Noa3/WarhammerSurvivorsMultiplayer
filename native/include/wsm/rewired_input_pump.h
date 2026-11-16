#pragma once
#include "wsm/main_thread_guard.h"
#include <cstdint>
#include <memory>
#include <string>

namespace wsm {
struct InputPumpContext {
    HWND window = nullptr;
    DWORD native_thread = 0;
    std::int32_t managed_thread = 0;
};
// The pump retains its client until process exit, including failed cleanup.
// Only request_stop and stopped may be called from producer threads.
class InputPumpClient {
public:
    virtual ~InputPumpClient() = default;
    // Called by the verified owner before start. Stateless observers need no
    // extra binding; configured engine clients must reject a different owner.
    virtual bool bind_context(const InputPumpContext&, std::string&) { return true; }
    virtual bool start(std::string& error) = 0;
    virtual void input_tick(std::uint64_t now_ms) = 0;
    // Independent engine-main-thread heartbeat. Must never write nonzero input.
    virtual void recovery_tick(std::uint64_t now_ms) = 0;
    virtual void request_stop() noexcept = 0;
    // True only after every client-owned engine resource is confirmed removed.
    virtual bool stopped() const noexcept = 0;
};
struct RewiredInputPumpState {
    bool started = false, subscription_pending = false, stop_requested = false;
    bool cleanup_complete = false, recovery_running = false, window_lost = false;
    std::uint64_t input_updates = 0, recovery_updates = 0;
    std::uint64_t foreign_callbacks = 0, reentrant_callbacks = 0;
    std::uint64_t last_input_ms = 0, last_recovery_ms = 0;
    std::string error;
};
// Address-stable, pinned process-lifetime event and recovery ownership.
// Wrapper destruction requests stop; it never destroys pending engine resources.
class RewiredInputPump {
public:
    RewiredInputPump();
    ~RewiredInputPump();
    RewiredInputPump(const RewiredInputPump&) = delete;
    RewiredInputPump& operator=(const RewiredInputPump&) = delete;
    // Resolves metadata only. Requires the caller's exact owned Unity window.
    bool initialize(HMODULE game, Il2CppApi api, Il2CppDomain* domain,
                    HMODULE bridge, HWND window, DWORD os_thread,
                    std::int32_t managed_thread, std::string& error);
    // Main-thread only. The client must not own live resources until start().
    // Ownership transfers even on a client-start/add exception, after the
    // independent recovery worker exists. Failed cleanup stays reachable.
    bool start(std::unique_ptr<InputPumpClient> client, std::string& error);
    void stop() noexcept;
    [[nodiscard]] RewiredInputPumpState state() const;
#ifdef WSM_PLAYER_STATE_TESTING
    // Fault injection is compiled only into original SDK fixtures.
    void test_fail_start(unsigned step);
#endif
private:
    struct Impl;
    Impl* impl_ = nullptr;
};
}
