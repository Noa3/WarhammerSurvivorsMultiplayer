#include "wsm/rewired_input_client.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <utility>

namespace wsm {
struct RewiredInputMailbox::State {
    explicit State(bool zero) : zero_only(zero) {}
    InputDriver driver;
    std::atomic<bool> stop_requested{false}, finished{false};
    const bool zero_only;
    mutable std::mutex resource_mutex;
    RewiredInputResourceState resource;
};
namespace {
class ControllerBackend final : public InputDriverBackend {
public:
    using Publish = std::function<void(RewiredOwnedControlState)>;
    explicit ControllerBackend(RewiredInputClientConfig config, Publish publish)
        : config_(std::move(config)), publish_(std::move(publish)) {}
    bool open(std::string& error) {
        if (!reader_.initialize(config_.game, config_.api, config_.domain,
                                config_.native_thread, config_.managed_thread, error)) return false;
        if (!control_.initialize(config_.game, config_.api, config_.domain,
                                 config_.native_thread, config_.managed_thread, error)) return false;
        initialized_ = true;
        capture();
        try { const bool result = control_.open(config_.options, error); capture(); return result; }
        catch (...) { capture(); throw; }
    }
    bool alive() {
        bool running = false;
        std::string error;
        if (reader_.read_run_state(running, error) != PlayerReadStatus::Captured || !running) return false;
        SceneStateSnapshot scene;
        if (reader_.read(scene, error) != PlayerReadStatus::Captured) return false;
        return std::count_if(scene.players.begin(), scene.players.end(), [&](const auto& actor) {
            return actor.slot == 1 && actor.input_id == config_.options.player_id && !actor.dead && actor.hp > 0;
        }) == 1;
    }
    bool focused() const { return GetForegroundWindow() == config_.window; }
    bool apply(float x, float y, std::string& error) override {
        if (config_.zero_only && (x != 0 || y != 0)) {
            error = "Zero-only input client refuses nonzero axes";
            return false;
        }
        try { const bool result = control_.set_axes(x, y, error); capture(); return result; }
        catch (...) { capture(); throw; }
    }
    bool close(std::string& error) override {
        try { const bool result = control_.close(error); capture(); return result; }
        catch (...) { capture(); throw; }
    }
private:
    void capture() { if (initialized_) publish_(control_.state()); }
    RewiredInputClientConfig config_;
    Publish publish_;
    bool initialized_ = false;
    PlayerStateReader reader_;
    RewiredControllerSession control_;
};
}
struct RewiredInputClient::Impl {
    RewiredInputClientConfig config;
    std::shared_ptr<RewiredInputMailbox::State> state;
    ControllerBackend* backend = nullptr; // borrowed only while the owning driver is active
    bool attempted = false, bound = false;
    std::uint64_t started_ms = 0;
    explicit Impl(RewiredInputClientConfig value) : config(std::move(value)),
        state(std::make_shared<RewiredInputMailbox::State>(config.zero_only)) {}
    void observe_completion() {
        if ((attempted || state->stop_requested.load(std::memory_order_acquire)) &&
            state->driver.snapshot().state == InputDriverState::idle) {
            backend = nullptr;
            state->finished.store(true, std::memory_order_release);
        }
    }
    void prepare_tick(std::uint64_t now) {
        if (attempted && config.zero_only && now >= started_ms && now - started_ms >= config.zero_only_max_ms)
            state->stop_requested.store(true, std::memory_order_release);
        if (state->stop_requested.load(std::memory_order_acquire)) {
            state->driver.stop();
        } else if (backend && !backend->alive()) {
            state->stop_requested.store(true, std::memory_order_release);
            state->driver.stop();
        }
    }
};
RewiredInputMailbox::RewiredInputMailbox(std::shared_ptr<State> state) : state_(std::move(state)) {}
bool RewiredInputMailbox::submit(std::uint64_t epoch, std::uint64_t sequence, float x, float y,
                                 std::uint64_t now, std::uint32_t duration) {
    return !state_->zero_only && !state_->stop_requested.load(std::memory_order_acquire) &&
        state_->driver.submit(epoch, sequence, x, y, now, duration);
}
void RewiredInputMailbox::stop() noexcept { state_->stop_requested.store(true, std::memory_order_release); }
InputDriverStatus RewiredInputMailbox::status() const { return state_->driver.snapshot(); }
RewiredInputResourceState RewiredInputMailbox::resource_state() const {
    std::lock_guard lock(state_->resource_mutex);
    return state_->resource;
}
bool RewiredInputMailbox::stopped() const noexcept { return state_->finished.load(std::memory_order_acquire); }
RewiredInputClient::RewiredInputClient(RewiredInputClientConfig config) : impl_(std::make_unique<Impl>(std::move(config))),
    mailbox_(new RewiredInputMailbox(impl_->state)) {}
RewiredInputClient::~RewiredInputClient() = default;
std::shared_ptr<RewiredInputMailbox> RewiredInputClient::mailbox() const { return mailbox_; }
bool RewiredInputClient::bind_context(const InputPumpContext& context, std::string& error) {
    auto& p = *impl_;
    error.clear();
    if (p.config.zero_only && (p.config.zero_only_max_ms == 0 || p.config.zero_only_max_ms > 30000)) {
        error = "Zero-only duration must be between 1 and 30000 milliseconds";
        return false;
    }
    DWORD pid = 0;
    if (p.attempted || p.config.window != context.window || p.config.native_thread != context.native_thread ||
        p.config.managed_thread != context.managed_thread || !context.window || context.managed_thread <= 0 ||
        context.native_thread != GetCurrentThreadId() ||
        GetWindowThreadProcessId(context.window, &pid) != context.native_thread || pid != GetCurrentProcessId()) {
        error = "Input client does not match the verified owned Unity pump context";
        return false;
    }
    p.bound = true;
    return true;
}
bool RewiredInputClient::start(std::string& error) {
    auto& p = *impl_;
    error.clear();
    if (!p.bound) { error = "Rewired input client requires its verified pump context"; return false; }
    if (p.state->stop_requested.load(std::memory_order_acquire)) { error = "Rewired input client stopped before start"; return false; }
    if (p.attempted) { error = "Rewired input client already started"; return false; }
    p.attempted = true;
    p.started_ms = GetTickCount64();
    // State owns the driver/backend and therefore outlives this callback. A
    // shared_ptr capture would instead create a driver/backend/state cycle.
    auto* const shared = p.state.get();
    auto value = std::make_unique<ControllerBackend>(p.config, [shared](RewiredOwnedControlState owned) {
        std::lock_guard lock(shared->resource_mutex);
        shared->resource.owned = std::move(owned);
        shared->resource.captured = true;
    });
    auto* backend = value.get();
    std::unique_ptr<InputDriverBackend> owned = std::move(value);
    if (!p.state->driver.start(p.config.epoch, p.config.options.player_id, owned)) {
        error = "Invalid or occupied Rewired input driver";
        p.state->stop_requested.store(true, std::memory_order_release);
        return false;
    }
    p.backend = backend;
    if (!backend->open(error)) {
        p.state->stop_requested.store(true, std::memory_order_release);
        p.state->driver.stop();
        return false;
    }
    return true;
}
void RewiredInputClient::input_tick(std::uint64_t now) {
    auto& p = *impl_;
    p.prepare_tick(now);
    const bool valid = !p.state->stop_requested.load(std::memory_order_acquire);
    p.state->driver.event_tick(now, valid && p.backend && p.backend->focused(), valid);
    p.observe_completion();
}
void RewiredInputClient::recovery_tick(std::uint64_t now) {
    auto& p = *impl_;
    p.prepare_tick(now);
    p.state->driver.recovery_tick(now);
    p.observe_completion();
}
void RewiredInputClient::request_stop() noexcept { mailbox_->stop(); }
bool RewiredInputClient::stopped() const noexcept { return mailbox_->stopped(); }
}
