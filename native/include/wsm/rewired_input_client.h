#pragma once
#include "wsm/input_driver.h"
#include "wsm/rewired_controller_session.h"
#include "wsm/rewired_input_pump.h"

namespace wsm {
struct RewiredInputClientConfig {
    HMODULE game = nullptr;
    Il2CppApi api{};
    Il2CppDomain* domain = nullptr;
    HWND window = nullptr;
    DWORD native_thread = 0;
    std::int32_t managed_thread = 0;
    std::uint64_t epoch = 0;
    RewiredControlOptions options;
    bool zero_only = false;
    std::uint32_t zero_only_max_ms = 15000;
};
// Captured from this client's initialized session on the verified pump thread,
// not a general observation of vendor resources.
struct RewiredInputResourceState {
    bool captured = false;
    RewiredOwnedControlState owned;
};
class RewiredInputClient;
// Producer access is data-only. Holding or dropping this mailbox does not move
// engine work to a producer thread; the process-lifetime pump owns the client.
class RewiredInputMailbox {
public:
    bool submit(std::uint64_t epoch, std::uint64_t sequence, float x, float y,
                std::uint64_t now_ms, std::uint32_t duration_ms);
    void stop() noexcept;
    [[nodiscard]] InputDriverStatus status() const;
    [[nodiscard]] RewiredInputResourceState resource_state() const;
    [[nodiscard]] bool stopped() const noexcept;
private:
    friend class RewiredInputClient;
    struct State;
    explicit RewiredInputMailbox(std::shared_ptr<State> state);
    std::shared_ptr<State> state_;
};
// Transfer into RewiredInputPump before start: no resources are acquired by the
// constructor. The driver owns the real controller backend before open is tried,
// including partial-open cleanup. All engine operations stay on the pump thread.
// An explicit strictly zero-only lifecycle diagnostic is an exception; all
// nonzero target-game control remains gated on input-phase verification.
class RewiredInputClient final : public InputPumpClient {
public:
    explicit RewiredInputClient(RewiredInputClientConfig config);
    ~RewiredInputClient() override;
    [[nodiscard]] std::shared_ptr<RewiredInputMailbox> mailbox() const;
    bool bind_context(const InputPumpContext& context, std::string& error) override;
    bool start(std::string& error) override;
    void input_tick(std::uint64_t now_ms) override;
    void recovery_tick(std::uint64_t now_ms) override;
    void request_stop() noexcept override;
    bool stopped() const noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::shared_ptr<RewiredInputMailbox> mailbox_;
};
}
