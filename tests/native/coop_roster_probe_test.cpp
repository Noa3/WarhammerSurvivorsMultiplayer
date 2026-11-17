// Execute the production parser/serializer/writer without a game or runtime.
#include "../../native/src/coop_roster_probe.cpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <thread>

namespace {
struct TeardownFixture {
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HWND window = nullptr;
    DWORD owner = 0;
    wsm::ProbeContext* expected = nullptr;
    std::atomic<int> calls{0};
    std::atomic<bool> resource_owned{true}, correct_context{false}, correct_thread{false};
};
TeardownFixture* teardown_fixture = nullptr;
LRESULT CALLBACK teardown_window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
void hold_teardown_admission(void* data) {
    auto& f = *static_cast<TeardownFixture*>(data);
    SetEvent(f.entered);
    WaitForSingleObject(f.release, 5000);
}
void simulated_teardown_failure(void* data) {
    auto& f = *teardown_fixture;
    ++f.calls;
    static_cast<wsm::ProbeContext*>(data)->action_succeeded = false;
}
void simulated_teardown(void* data) {
    auto& f = *teardown_fixture;
    f.correct_context = data == f.expected;
    f.correct_thread = GetCurrentThreadId() == f.owner;
    ++f.calls;
    f.resource_owned = false;
    static_cast<wsm::ProbeContext*>(data)->action_succeeded = true;
}

int dispatch_hook_calls = 0;
wsm::HookResult dispatch_injected = wsm::HookResult::failed;
wsm::HookResult unavailable_dispatch(HMODULE, HWND, DWORD, void (*)(void*), void*, DWORD) {
    ++dispatch_hook_calls;
    return dispatch_injected;
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    int failed = 0, count = 0;
    auto check = [&](const char* name, bool ok) { ++count; if (!ok) ++failed; std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; };
    using namespace wsm;
    {
        TeardownFixture f;
        if (!f.ready || !f.entered || !f.release || !f.finished || !f.started) return 2;
        teardown_fixture = &f;
        std::thread target([&] {
            f.owner = GetCurrentThreadId();
            WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = teardown_window_proc;
            cls.lpszClassName = L"WSMRosterTeardownFixture";
            if (RegisterClassW(&cls)) f.window = CreateWindowW(cls.lpszClassName, L"fixture", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
            SetEvent(f.ready);
            if (f.window) { MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg); }
        });
        if (WaitForSingleObject(f.ready, 5000) != WAIT_OBJECT_0 || !f.window) { target.join(); return 2; }
        ProbeContext owned; owned.window.hwnd = f.window; owned.window.thread = f.owner;
        f.expected = &owned;
        const auto module = GetModuleHandleW(nullptr);
        HookResult first = HookResult::failed;
        std::thread blocker([&] { first = run_window_hook(module, f.window, f.owner, hold_teardown_admission, &f, 3000); });
        const bool held = WaitForSingleObject(f.entered, 4000) == WAIT_OBJECT_0;
        check("teardown fixture holds real global hook", held && run_window_hook(module, f.window, f.owner, simulated_teardown, &owned, 20) == HookResult::busy);
        const auto expired = dispatch_roster_teardown(module, owned, simulated_teardown, 80);
        check("teardown busy deadline retains owned context and resource", expired == HookResult::busy && f.calls == 0 && f.resource_owned && !owned.action_succeeded);
        HookResult result = HookResult::failed;
        std::thread cleanup([&] { SetEvent(f.started); result = dispatch_roster_teardown(module, owned, simulated_teardown); SetEvent(f.finished); });
        const bool started = WaitForSingleObject(f.started, 2000) == WAIT_OBJECT_0;
        const bool waiting = started && WaitForSingleObject(f.finished, 150) == WAIT_TIMEOUT;
        check("teardown waits while real hook is busy", waiting && f.calls == 0 && f.resource_owned && !owned.action_succeeded);
        SetEvent(f.release);
        cleanup.join(); blocker.join();
        check("teardown completes once on owner with retained context", first == HookResult::completed && result == HookResult::completed &&
              f.calls == 1 && f.correct_context && f.correct_thread && !f.resource_owned && owned.action_succeeded);
        f.calls = 0; f.resource_owned = true; owned.action_succeeded = false;
        const auto invalid = dispatch_roster_teardown(nullptr, owned, simulated_teardown);
        check("teardown invalid hook retains owner without success", invalid == HookResult::failed && f.calls == 0 && f.resource_owned && !owned.action_succeeded);
        const auto close_failure = dispatch_roster_teardown(module, owned, simulated_teardown_failure);
        check("completed hook does not imply cleanup succeeded", close_failure == HookResult::completed && f.calls == 1 && f.resource_owned && !owned.action_succeeded);
        HWND idle = CreateWindowW(L"WSMRosterTeardownFixture", L"idle", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, module, nullptr);
        ProbeContext idle_context; idle_context.window.hwnd = idle; idle_context.window.thread = GetCurrentThreadId();
        f.expected = &idle_context; f.calls = 0;
        HookResult timeout = HookResult::failed;
        std::thread no_pump([&] { timeout = dispatch_roster_teardown(module, idle_context, simulated_teardown, 80); });
        no_pump.join();
        MSG pending{}; while (PeekMessageW(&pending, idle, 0, 0, PM_REMOVE)) DispatchMessageW(&pending);
        check("teardown timeout retains owner and prevents late callback", idle && timeout == HookResult::timeout && f.calls == 0 && f.resource_owned && !idle_context.action_succeeded);
        if (idle) DestroyWindow(idle);
        PostMessageW(f.window, WM_CLOSE, 0, 0); target.join();
        CloseHandle(f.ready); CloseHandle(f.entered); CloseHandle(f.release); CloseHandle(f.finished); CloseHandle(f.started);
        teardown_fixture = nullptr;
    }
    RosterCommand command;
    wchar_t previous_hook_flag[128]{};
    const DWORD previous_hook_length=GetEnvironmentVariableW(L"WSM_UPGRADE_PASS_HOOK_OPT_IN",previous_hook_flag,128);
    if(previous_hook_length>=128){std::cerr<<"Cannot preserve oversized test flag\n";return 1;}
    check("unset upgrade permission",SetEnvironmentVariableW(L"WSM_UPGRADE_PASS_HOOK_OPT_IN",nullptr)&&!upgrade_hook_opt_in());
    check("explicit separate upgrade permission",SetEnvironmentVariableW(L"WSM_UPGRADE_PASS_HOOK_OPT_IN",L"1")&&upgrade_hook_opt_in());
    check("ambiguous upgrade permission refused",SetEnvironmentVariableW(L"WSM_UPGRADE_PASS_HOOK_OPT_IN",L"true")&&!upgrade_hook_opt_in());
    check("extended upgrade permission refused",SetEnvironmentVariableW(L"WSM_UPGRADE_PASS_HOOK_OPT_IN",L"1 ")&&!upgrade_hook_opt_in());
    check("restore process-local test flag",SetEnvironmentVariableW(L"WSM_UPGRADE_PASS_HOOK_OPT_IN",previous_hook_length?previous_hook_flag:nullptr)!=FALSE);
    check("inspect command", parse_command("inspect\r\n", 1234, command) && command.action == RosterAction::inspect);
    check("assigned custom source discovery route", parse_command("controlSources:1234", 1234, command));
    check("zero-only explicit source route preserves definition ID", parse_command("inputZeroStart:1234:7", 1234, command) && command.control.source_id == 7);
    check("zero-only explicit source accepts actual definition zero", parse_command("inputZeroStart:1234:0", 1234, command) && command.control.source_id == 0);
    for (const auto* bad : {"controlSources:1235", "controlSources:1234:0", "inputZeroStart:1235:7", "inputZeroStart:1234:",
                           "inputZeroStart:1234:-1", "inputZeroStart:1234:-0", "inputZeroStart:1234:+1", "inputZeroStart:1234:2147483648",
                           "inputZeroStart:1234:7:8", "inputZeroStart:1234: 7", "inputZeroStart:1234:7x"})
        check("source discovery and explicit source refuse malformed or foreign identity", !parse_command(bad, 1234, command));
    check("neutral input watch start route", parse_command("inputWatchStart:1234", 1234, command));
    check("neutral input watch state route", parse_command("inputWatchState:1234", 1234, command));
    check("neutral input watch stop route", parse_command("inputWatchStop:1234", 1234, command));
    check("zero-only controller start route", parse_command("inputZeroStart:1234", 1234, command) && command.control.source_id == -1);
    check("zero-only controller state route", parse_command("inputZeroState:1234", 1234, command));
    check("zero-only controller stop route", parse_command("inputZeroStop:1234", 1234, command));
    for (const auto* bad : {"inputZeroStart:1235", "inputZeroStart:1234:0.5:0", "inputZeroSubmit:1234", "inputZeroState:1234:extra", "inputZeroStop:-1234"})
        check("zero-only controller refuses input/foreign identity", !parse_command(bad, 1234, command));
    for (const auto* bad : {"inputWatchStart:1235", "inputWatchState:1234:extra", "inputWatchStop:-1234", "inputWatchStart:1234\ninspect"})
        check("neutral input watch refuses malformed identity", !parse_command(bad, 1234, command));
    {
        wchar_t previous[128]{};
        const DWORD length = GetEnvironmentVariableW(L"WSM_INPUT_PUMP_OBSERVATION_OPT_IN", previous, 128);
        if (length >= 128) return 1;
        for (const auto* flag : {static_cast<const wchar_t*>(nullptr), L"true", L"01", L"1 "}) {
            check("set nonexact input-watch permission", SetEnvironmentVariableW(L"WSM_INPUT_PUMP_OBSERVATION_OPT_IN", flag) != FALSE);
            ProbeContext denied; denied.command.action = RosterAction::input_watch_start;
            execute_command(&denied);
            check("input watch subscription refused before runtime", !denied.action_succeeded && !denied.input_pump &&
                  denied.action_error == "Input event observation requires separate WSM_INPUT_PUMP_OBSERVATION_OPT_IN=1");
        }
        check("exact input-watch permission", SetEnvironmentVariableW(L"WSM_INPUT_PUMP_OBSERVATION_OPT_IN", L"1") && input_watch_opt_in());
        ProbeContext guard; guard.command.action = RosterAction::input_watch_start;
        execute_command(&guard);
        check("input watch cannot bypass main-thread proof", !guard.action_succeeded && !guard.input_pump && guard.action_error == "Native OS thread mismatch.");
        for (const auto action : {RosterAction::input_watch_state, RosterAction::input_watch_stop}) {
            ProbeContext data; data.command.action = action;
            execute_command(&data);
            const auto json = result_json(data, 1);
            check("input watch absent status and stop are data-only", data.action_succeeded && !data.input_watch_captured && !data.input_pump && data.action_error.empty());
            check("input watch never claims control or ordering", json.find("\"inputEventWatch\":{\"stateCaptured\":false,\"nonzeroEnabled\":false,\"consumerOrderingProven\":false}") != std::string::npos);
        }
        ProbeContext view; view.command.action = RosterAction::input_watch_state; view.input_watch_captured = true;
        view.input_watch_state.input_updates = (std::numeric_limits<std::uint64_t>::max)();
        view.input_watch_state.recovery_updates = 42; view.input_watch_state.error = "quoted \"diagnostic\"";
        const auto json = result_json(view, 1);
        check("input watch serializes exact data snapshot", json.find("\"inputUpdates\":\"18446744073709551615\"") != std::string::npos && json.find("\"recoveryUpdates\":\"42\"") != std::string::npos && json.find("quoted \\\"diagnostic\\\"") != std::string::npos);
        view.input_watch_captured = false;
        check("input watch unavailable suppresses stale counters", result_json(view, 2).find("inputUpdates") == std::string::npos);
        check("restore input-watch permission", SetEnvironmentVariableW(L"WSM_INPUT_PUMP_OBSERVATION_OPT_IN", length ? previous : nullptr) != FALSE);
    }
    {
        wchar_t previous[128]{};
        const DWORD length = GetEnvironmentVariableW(L"WSM_INPUT_ZERO_TRIAL_OPT_IN", previous, 128);
        if (length >= 128) return 1;
        for (const auto* flag : {static_cast<const wchar_t*>(nullptr), L"true", L"01", L"1 "}) {
            check("set nonexact zero-only permission", SetEnvironmentVariableW(L"WSM_INPUT_ZERO_TRIAL_OPT_IN", flag) != FALSE);
            ProbeContext denied; denied.command.action = RosterAction::input_zero_start;
            execute_command(&denied);
            check("zero-only start refuses before runtime acquisition", !denied.action_succeeded && !denied.input_pump && !denied.zero_mailbox &&
                  denied.action_error == "Zero-only controller trial requires separate WSM_INPUT_ZERO_TRIAL_OPT_IN=1");
        }
        check("exact zero-only permission", SetEnvironmentVariableW(L"WSM_INPUT_ZERO_TRIAL_OPT_IN", L"1") && input_zero_opt_in());
        ProbeContext guard; guard.command.action = RosterAction::input_zero_start;
        execute_command(&guard);
        check("zero-only start cannot bypass main-thread proof", !guard.action_succeeded && !guard.input_pump && !guard.zero_mailbox && guard.action_error == "Native OS thread mismatch.");
        check("restore zero-only permission", SetEnvironmentVariableW(L"WSM_INPUT_ZERO_TRIAL_OPT_IN", length ? previous : nullptr) != FALSE);
    }
    {
        RewiredInputProfileSnapshot profile;
        profile.horizontal_action_id = 101; profile.vertical_action_id = 103;
        profile.definitions.push_back({55, {17, 23}});
        RewiredControlEnvironment env;
        env.horizontal_action_id = 101; env.vertical_action_id = 103;
        env.categories.push_back({32, "movement"}); env.layouts.push_back({37, "custom"});
        RewiredPlayerInputState primary; primary.player_id = 0;
        RewiredMapState map; map.enabled = true; map.category_id = 32;
        RewiredActionBinding horizontal; horizontal.action_id = 101;
        RewiredActionBinding vertical; vertical.action_id = 103;
        map.bindings = {horizontal, vertical}; primary.maps.push_back(map); env.players.push_back(primary);
        RewiredControlOptions selected; std::string error;
        check("zero options propagate only actual live IDs", select_zero_options(profile, env, selected, error) && error.empty() &&
              selected.player_id == 1 && selected.source_id == 55 && selected.horizontal_axis_id == 17 && selected.vertical_axis_id == 23 && selected.category_id == 32 && selected.layout_id == 37);
        const auto valid = selected;
        auto missing = profile; missing.definitions.clear();
        check("zero options refuse missing source without overwriting selection", !select_zero_options(missing, env, selected, error) && selected.source_id == valid.source_id);
        auto ambiguous = profile; ambiguous.definitions.push_back({56, {41, 43}});
        check("zero options refuse ambiguous sources", !select_zero_options(ambiguous, env, selected, error));
        check("explicit zero source selects only the chosen live definition", select_zero_options(ambiguous, env, selected, error, 56) &&
              selected.source_id == 56 && selected.horizontal_axis_id == 41 && selected.vertical_axis_id == 43);
        const auto explicitly_selected = selected;
        std::reverse(ambiguous.definitions.begin(), ambiguous.definitions.end());
        check("explicit zero source is independent of catalog enumeration order", select_zero_options(ambiguous, env, selected, error, 56) &&
              selected.source_id == 56 && selected.horizontal_axis_id == 41);
        check("explicit absent source never falls back or overwrites previous selection", !select_zero_options(ambiguous, env, selected, error, 99) &&
              selected.source_id == explicitly_selected.source_id && selected.horizontal_axis_id == explicitly_selected.horizontal_axis_id);
        auto duplicated_source = ambiguous; duplicated_source.definitions.push_back({56, {71, 73}});
        check("explicit duplicate source refuses before selection", !select_zero_options(duplicated_source, env, selected, error, 56));
        auto unsuitable_source = ambiguous; unsuitable_source.definitions[0].axis_ids = {41, 41};
        check("explicit unsuitable source never substitutes another eligible definition", !select_zero_options(unsuitable_source, env, selected, error, 56));
        check("explicit source rejects invalid direct negative values", !select_zero_options(profile, env, selected, error, -2));
        auto changed_actions = env; changed_actions.vertical_action_id = 104;
        check("explicit source cannot bypass action identity", !select_zero_options(ambiguous, changed_actions, selected, error, 56));
        auto changed_category = env; changed_category.players[0].maps[0].enabled = false;
        check("explicit source cannot bypass primary movement category", !select_zero_options(ambiguous, changed_category, selected, error, 56));
        auto changed_layout = env; changed_layout.layouts.push_back({38, "other"});
        check("explicit source cannot bypass layout ambiguity", !select_zero_options(ambiguous, changed_layout, selected, error, 56));
        auto source_zero = profile; source_zero.definitions.push_back({0, {71, 73}});
        check("explicit definition zero is not confused with automatic selection", select_zero_options(source_zero, env, selected, error, 0) && selected.source_id == 0);
        auto duplicate_axes = profile; duplicate_axes.definitions[0].axis_ids = {17, 17};
        check("zero options refuse repeated axis IDs", !select_zero_options(duplicate_axes, env, selected, error));
        auto mismatched = profile; mismatched.vertical_action_id = 104;
        check("zero options refuse inconsistent action metadata", !select_zero_options(mismatched, env, selected, error));
        auto disabled = env; disabled.players[0].maps[0].enabled = false;
        check("zero options require enabled existing primary movement", !select_zero_options(profile, disabled, selected, error));
        auto no_vertical = env; no_vertical.players[0].maps[0].bindings.pop_back();
        check("zero options require both movement actions", !select_zero_options(profile, no_vertical, selected, error));
        auto layouts = env; layouts.layouts.push_back({38, "other"});
        check("zero options refuse ambiguous layouts", !select_zero_options(profile, layouts, selected, error));
        ProbeContext retained;
        retained.command.action = RosterAction::input_zero_start;
        retained.input_profile = profile;
        retained.input_profile.definitions[0].source_id = 56;
        retained.control_environment = env;
        retained.zero_options = valid; retained.zero_options_captured = true; retained.zero_epoch = 7;
        RewiredInputClientConfig old_config; old_config.zero_only = true; old_config.epoch = 7;
        old_config.options = valid;
        auto old_client = std::make_unique<RewiredInputClient>(old_config);
        retained.zero_mailbox = old_client->mailbox();
        retained.input_pump = std::make_unique<RewiredInputPump>(); retained.input_owner = InputProbeOwner::zero;
        const auto old_mailbox = retained.zero_mailbox;
        const auto* old_pump = retained.input_pump.get();
        retained.input_profile.definitions.push_back({57, {41, 43}});
        retained.command.control.source_id = 56;
        check("zero retry reaches real pump initialization failure", !start_zero_input_owner(retained) && !retained.action_error.empty());
        check("explicit zero source reaches pump preparation rather than ambiguous auto selection", retained.action_error != "Zero trial source definition is ambiguous");
        check("zero failed initialization retains previous pump and mailbox", retained.input_pump.get() == old_pump &&
              retained.zero_mailbox == old_mailbox && retained.input_owner == InputProbeOwner::zero && retained.zero_epoch == 7);
        check("zero failed initialization preserves retained owner options", retained.zero_options_captured &&
              retained.zero_options.source_id == valid.source_id && retained.zero_options.category_id == valid.category_id &&
              retained.zero_options.horizontal_axis_id == valid.horizontal_axis_id);
        capture_zero_state(retained);
        check("zero failed restart response does not pair candidate options with previous owner", result_json(retained, 1).find("\"sourceId\":55") != std::string::npos &&
              result_json(retained, 1).find("\"sourceId\":56") == std::string::npos);
        const auto retained_json = result_json(retained, 1);
        check("zero failed start labels retained owner epoch", retained_json.find("\"ownerEpoch\":\"7\"") != std::string::npos &&
              retained_json.find("\"optionsScope\":\"retainedOwner\"") != std::string::npos);
        retained.command.action = RosterAction::input_zero_state;
        check("zero state labels current owner rather than failed request", result_json(retained, 1).find("\"optionsScope\":\"owner\"") != std::string::npos);
        retained.input_profile.definitions.clear();
        check("zero selection refusal preserves previous epoch and options", !start_zero_input_owner(retained) &&
              retained.zero_epoch == 7 && retained.zero_options.source_id == valid.source_id && retained.zero_mailbox == old_mailbox);
        retained.input_owner = InputProbeOwner::watch;
        check("watch replacement suppresses previous zero owner's options", result_json(retained, 1).find("\"optionsCaptured\":false") != std::string::npos &&
              result_json(retained, 1).find("\"ownerEpoch\":") == std::string::npos);
        ProbeContext exhausted; exhausted.input_profile = profile; exhausted.control_environment = env;
        exhausted.zero_epoch = (std::numeric_limits<std::uint64_t>::max)();
        check("zero epoch exhaustion refuses before owner installation", !start_zero_input_owner(exhausted) &&
              exhausted.action_error == "Zero trial epoch space exhausted" && !exhausted.zero_options_captured &&
              !exhausted.input_pump && !exhausted.zero_mailbox && exhausted.zero_epoch == (std::numeric_limits<std::uint64_t>::max)());
        ProbeContext first_attempt; first_attempt.input_profile = profile; first_attempt.control_environment = env;
        check("zero initial failed initialization has no captured owner options", !start_zero_input_owner(first_attempt) &&
              !first_attempt.zero_options_captured && !first_attempt.input_pump && !first_attempt.zero_mailbox);
        ProbeContext unowned; unowned.command.action = RosterAction::input_zero_state;
        unowned.zero_options = valid; unowned.zero_options_captured = true;
        const auto unowned_json = result_json(unowned, 1);
        check("zero response suppresses unowned historical options", unowned_json.find("\"optionsCaptured\":false") != std::string::npos &&
              unowned_json.find("\"options\":{") == std::string::npos);
        unowned.input_owner = InputProbeOwner::zero;
        check("zero response refuses options without a positive owner epoch", result_json(unowned, 1).find("\"optionsCaptured\":false") != std::string::npos);
    }
    for (const auto failure : {HookResult::failed, HookResult::timeout, HookResult::busy}) {
        dispatch_injected = failure;
        for (const auto action : {RosterAction::input_zero_state, RosterAction::input_zero_stop}) {
            ProbeContext data; data.command.action = action;
            RewiredInputClientConfig config; config.zero_only = true;
            auto client = std::make_unique<RewiredInputClient>(config);
            data.zero_mailbox = client->mailbox(); data.input_pump = std::make_unique<RewiredInputPump>();
            data.input_owner = InputProbeOwner::zero;
            dispatch_hook_calls = 0;
            const auto result = dispatch_roster_command(nullptr, data, unavailable_dispatch);
            check("zero status and stop bypass unavailable engine dispatch", result == HookResult::completed && dispatch_hook_calls == 0 && data.action_succeeded && data.zero_captured);
            check("unstarted zero status does not fabricate resource inspection", !data.zero_resources.captured && data.zero_driver_state.epoch == 0 && data.zero_driver_state.nonzero_attempts == 0);
            const auto json = result_json(data, 1);
            check("zero response leaves nonzero and ordering disabled", json.find("\"zeroOnly\":true,\"nonzeroEnabled\":false,\"consumerOrderingProven\":false") != std::string::npos);
        }
    }
    {
        ProbeContext view; view.command.action = RosterAction::input_zero_state; view.zero_captured = true;
        view.zero_driver_state.nonzero_attempts = (std::numeric_limits<std::uint64_t>::max)();
        view.zero_resources.captured = true; view.zero_resources.owned.factory_outcome_unknown = true;
        view.zero_resources.owned.horizontal = std::numeric_limits<float>::quiet_NaN();
        view.zero_resources.owned.observation_error = "retained \"factory\"";
        view.control = std::make_unique<RewiredControllerSession>();
        const auto json = result_json(view, 1);
        check("zero snapshot serializes exact counters and uncertainty", json.find("\"nonzeroAttempts\":\"18446744073709551615\"") != std::string::npos && json.find("\"factoryOutcomeUnknown\":true") != std::string::npos);
        check("zero snapshot escapes diagnostics and rejects nonfinite JSON numbers", json.find("retained \\\"factory\\\"") != std::string::npos && json.find("\"x\":null") != std::string::npos);
        check("zero response excludes unrelated controller getter", json.find("\"ownedControl\":") == std::string::npos);
        view.zero_captured = false;
        check("zero unavailable snapshot suppresses stale resources", result_json(view, 2).find("\"resources\":") == std::string::npos);
    }
    for (const auto failure : {HookResult::failed, HookResult::timeout, HookResult::busy}) {
        dispatch_injected = failure;
        for (const auto action : {RosterAction::input_watch_state, RosterAction::input_watch_stop}) {
            ProbeContext data; data.command.action = action;
            data.input_pump = std::make_unique<RewiredInputPump>();
            dispatch_hook_calls = 0;
            const auto result = dispatch_roster_command(nullptr, data, unavailable_dispatch);
            check("production watch dispatch bypasses busy or stalled hook", result == HookResult::completed &&
                  dispatch_hook_calls == 0 && data.action_succeeded && data.input_watch_captured);
        }
        for (const auto action : {RosterAction::input_watch_start, RosterAction::inspect}) {
            ProbeContext engine; engine.command.action = action;
            dispatch_hook_calls = 0;
            check("engine actions still require actual hook", dispatch_roster_command(nullptr, engine, unavailable_dispatch) == failure && dispatch_hook_calls == 1 && !engine.action_succeeded);
        }
    }
    for (const auto action : {RosterAction::input_watch_start, RosterAction::input_watch_state, RosterAction::input_watch_stop}) {
        ProbeContext cached; cached.command.action = action;
        cached.control = std::make_unique<RewiredControllerSession>();
        check("watch response omits unrelated cached controller and axes", result_json(cached, 1).find("\"ownedControl\":") == std::string::npos);
    }
    {
        wchar_t previous[128]{};
        const DWORD length=GetEnvironmentVariableW(L"WSM_CAMERA_FOLLOW_OPT_IN",previous,128);
        if(length>=128)return 1;
        check("unset camera permission",SetEnvironmentVariableW(L"WSM_CAMERA_FOLLOW_OPT_IN",nullptr)!=FALSE);
        for(const auto* route:{"cameraOpen:1234:41:own","cameraOpen:1234:0:shared","cameraMode:1234:own","cameraMode:1234:shared","cameraState:1234","cameraClose:1234"}) {
            RosterCommand parsed;
            const bool valid=parse_command(route,1234,parsed);
            check(route,valid && !mutates_roster(parsed.action));
            if(valid && (std::string_view(route).substr(0,10)=="cameraOpen" || std::string_view(route).substr(0,10)=="cameraMode")) {
                ProbeContext refused;refused.command=parsed;execute_command(&refused);
                check("camera mutation denied before runtime",!refused.action_succeeded && refused.action_error=="Camera follow requires separate WSM_CAMERA_FOLLOW_OPT_IN=1");
            }
        }
        for(const auto* bad:{"cameraOpen:1235:1:own","cameraOpen:1234:-1:own","cameraOpen:1234:2147483648:own","cameraOpen:1234:1:own:extra","cameraOpen:1234:1:other","cameraMode:1234:own:extra","cameraMode:1235:shared","cameraMode:1234:","cameraState:1235","cameraState:1234:0","cameraClose:1234:0","cameraClose:-1234","cameraUnknown:1234"})
            check(bad,!parse_command(bad,1234,command));
        check("exact camera permission",SetEnvironmentVariableW(L"WSM_CAMERA_FOLLOW_OPT_IN",L"1")&&camera_opt_in());
        for(const auto* flag:{L"true",L"1 ",L"01"}) {
            check("camera rejects nonexact permission",SetEnvironmentVariableW(L"WSM_CAMERA_FOLLOW_OPT_IN",flag)&&!camera_opt_in());
            for(const auto action:{RosterAction::camera_open,RosterAction::camera_mode}) {
                ProbeContext refused;refused.command.action=action;execute_command(&refused);
                check("nonexact camera permission denied before runtime",!refused.action_succeeded&&refused.action_error=="Camera follow requires separate WSM_CAMERA_FOLLOW_OPT_IN=1"&&!refused.camera);
            }
        }
        for(const auto action:{RosterAction::camera_state,RosterAction::camera_close}) {
            ProbeContext observation;observation.command.action=action;execute_command(&observation);
            check("camera observation and cleanup proceed to thread guard without opt-in",!observation.action_succeeded&&observation.action_error=="Native OS thread mismatch.");
        }
        check("open preserves input and shared mode",parse_command("cameraOpen:1234:41:shared",1234,command)&&command.action==RosterAction::camera_open&&command.player==41&&command.camera_mode==CameraFollowMode::SharedAlive);
        check("mode preserves own mode",parse_command("cameraMode:1234:own",1234,command)&&command.action==RosterAction::camera_mode&&command.camera_mode==CameraFollowMode::OwnPlayer);
        ProbeContext cached;cached.command.action=RosterAction::camera_state;cached.camera_state_captured=true;
        cached.camera_state.attached=true;cached.camera_state.has_target=true;cached.camera_state.spectating=true;
        cached.camera_state.local_input_id=41;cached.camera_state.target_input_id=52;cached.camera_state.mode=CameraFollowMode::SharedAlive;
        cached.camera_state.updates=(std::numeric_limits<std::uint64_t>::max)();cached.camera_state.x=1.00000012f;cached.camera_state.y=-4.25f;cached.camera_state.error="escaped \"error\"";
        const auto json=result_json(cached,1);
        check("cached camera status includes all fields losslessly",json.find("\"updates\":\"18446744073709551615\"")!=std::string::npos&&json.find("\"x\":1.00000012")!=std::string::npos&&json.find("\"targetInputId\":52")!=std::string::npos&&json.find("\"spectating\":true")!=std::string::npos&&json.find("escaped \\\"error\\\"")!=std::string::npos);
        cached.camera_state_captured=false;
        check("unavailable camera state suppresses stale fields",result_json(cached,2).find("\"camera\":{\"stateCaptured\":false}")!=std::string::npos);
        check("restore camera permission",SetEnvironmentVariableW(L"WSM_CAMERA_FOLLOW_OPT_IN",length?previous:nullptr)!=FALSE);
    }
    check("own PID enrollment", parse_command("enroll:1234:41", 1234, command) && command.action == RosterAction::enroll && command.player == 41);
    check("own PID rollback", parse_command("rollback:1234", 1234, command) && command.action == RosterAction::rollback);
    check("own PID menu player selection", parse_command("select:1234:41", 1234, command) && command.action == RosterAction::select_player && command.player == 41);
    check("wrong PID menu player selection", !parse_command("select:1235:41", 1234, command));
    check("own PID stock character copy", parse_command("copy:1234:41", 1234, command) && command.action == RosterAction::copy_character && command.player == 41);
    check("wrong PID stock character copy", !parse_command("copy:1235:41", 1234, command));
    check("own PID upgrade context query", parse_command("upgradeContext:1234", 1234, command) && command.action==RosterAction::upgrade_context && command.player==-1);
    check("wrong PID upgrade context query", !parse_command("upgradeContext:1235", 1234, command));
    check("own PID explicit selection query", parse_command("upgradeSelection:1234",1234,command) && command.action==RosterAction::upgrade_selection && command.player==-1);
    check("wrong PID explicit selection query", !parse_command("upgradeSelection:1235",1234,command));
    check("selection query cannot enter mutation dispatch", !mutates_roster(RosterAction::upgrade_selection));
    RosterCommand item_command;
    check("own PID explicit item query",parse_command("upgradeItems:1234",1234,item_command) && item_command.player==-1);
    check("wrong PID explicit item query",!parse_command("upgradeItems:1235",1234,item_command));
    check("item query cannot enter mutation dispatch",item_command.action==RosterAction::upgrade_items && !mutates_roster(item_command.action));
    RosterCommand hint_command;
    check("own PID explicit hint query",parse_command("upgradeHint:1234",1234,hint_command) && hint_command.action==RosterAction::upgrade_hint);
    check("wrong PID explicit hint query",!parse_command("upgradeHint:1235",1234,hint_command));
    check("hint query cannot enter mutation dispatch",!mutates_roster(RosterAction::upgrade_hint));
    RosterCommand equipment_command;
    check("own PID explicit equipment query",parse_command("equipment:1234",1234,equipment_command) && equipment_command.action==RosterAction::equipment && equipment_command.player==-1);
    check("wrong PID explicit equipment query",!parse_command("equipment:1235",1234,equipment_command));
    check("equipment query cannot enter mutation dispatch",!mutates_roster(RosterAction::equipment));
    RosterCommand profile_command;
    check("own PID explicit input profile query",parse_command("inputProfile:1234",1234,profile_command)&&profile_command.action==RosterAction::input_profile&&profile_command.player==-1);
    check("wrong PID explicit input profile query",!parse_command("inputProfile:1235",1234,profile_command));
    check("input profile cannot enter mutation dispatch",!mutates_roster(RosterAction::input_profile));
    check("own PID controller environment route",parse_command("controlEnvironment:1234",1234,command));
    check("own PID controller open route",parse_command("controlOpen:1234:7:17:23:2:9",1234,command));
    check("own PID bounded controller pulse route",parse_command("controlPulse:1234:0.5:-0.25:120",1234,command));
    check("own PID controller cleanup route",parse_command("controlClose:1234",1234,command));
    check("own PID upgrade hook inspection route",parse_command("upgradeHook:1234",1234,command));
    check("own PID upgrade hook install route",parse_command("upgradeHookInstall:1234",1234,command));
    check("own PID upgrade hook removal route",parse_command("upgradeHookRemove:1234",1234,command));
    check("own PID current upgrade offer route",parse_command("upgradeOffer:1234:52",1234,command));
    check("own PID bound upgrade choice route",parse_command("upgradeChoose:1234:27:52:0:7001",1234,command));
    check("upgrade choice preserves explicit owner and token",command.action==RosterAction::upgrade_choose&&command.choice_token==27&&command.player==52&&command.choice_index==0&&command.choice_weapon==7001);
    check("upgrade choice closes only own PID",parse_command("upgradeChoiceClose:1234",1234,command)&&command.action==RosterAction::upgrade_choice_close&&!parse_command("upgradeChoiceClose:1235",1234,command));
    for(const auto* value:{"upgradeOffer:1235:52","upgradeOffer:1234:-1","upgradeOffer:1234:52:0",
        "upgradeChoose:1235:27:52:0:7001","upgradeChoose:1234:0:52:0:7001","upgradeChoose:1234:27:52:-1:7001",
        "upgradeChoose:1234:27:52:32:7001","upgradeChoose:1234:27:52:0:0","upgradeChoose:1234:27:52:0:7001:9",
        "upgradeChoose:1234:27:52:0:7001\ninspect","upgradeChoiceClose:1234:0"})
        check(value,!parse_command(value,1234,command));
    const auto max_token=std::to_string((std::numeric_limits<std::uint64_t>::max)());
    check("upgrade choice accepts exact UInt64 token",parse_command("upgradeChoose:1234:"+max_token+":52:0:7001",1234,command)&&command.choice_token==(std::numeric_limits<std::uint64_t>::max)());
    check("upgrade choice rejects token overflow",!parse_command("upgradeChoose:1234:"+max_token+"0:52:0:7001",1234,command));
    wchar_t previous_choice_flag[128]{};
    const DWORD previous_choice_length=GetEnvironmentVariableW(L"WSM_UPGRADE_CHOICE_OPT_IN",previous_choice_flag,128);
    if(previous_choice_length>=128)return 1;
    check("choice permission defaults closed",SetEnvironmentVariableW(L"WSM_UPGRADE_CHOICE_OPT_IN",nullptr)&&!upgrade_choice_opt_in());
    for(const auto action:{RosterAction::upgrade_offer,RosterAction::upgrade_choose}) {
        ProbeContext refused;refused.command.action=action;execute_command(&refused);
        check("choice refuses before touching uninitialized runtime",!refused.action_succeeded&&!refused.choice_result.invocation_attempted&&refused.action_error=="Upgrade selection requires separate WSM_UPGRADE_CHOICE_OPT_IN=1");
    }
    check("choice permission accepts exact one",SetEnvironmentVariableW(L"WSM_UPGRADE_CHOICE_OPT_IN",L"1")&&upgrade_choice_opt_in());
    check("choice permission rejects truthy text",SetEnvironmentVariableW(L"WSM_UPGRADE_CHOICE_OPT_IN",L"true")&&!upgrade_choice_opt_in());
    check("choice permission restores prior value",SetEnvironmentVariableW(L"WSM_UPGRADE_CHOICE_OPT_IN",previous_choice_length?previous_choice_flag:nullptr)!=FALSE);
    ProbeContext choice_json;choice_json.command.action=RosterAction::upgrade_offer;
    choice_json.choice_status=PlayerReadStatus::Captured;choice_json.choice_offer.token=(std::numeric_limits<std::uint64_t>::max)();
    choice_json.choice_offer.input_id=52;choice_json.choice_offer.actor_level=5;choice_json.choice_offer.cards={{0,7001,1,true,true}};
    const auto offer_json=result_json(choice_json,0);
    check("offer token serializes losslessly as string",offer_json.find("\"token\":\""+max_token+"\"")!=std::string::npos&&offer_json.find("\"weaponType\":7001")!=std::string::npos);
    choice_json.choice_status=PlayerReadStatus::Error;
    check("refused offer suppresses stale cards",result_json(choice_json,0).find("\"token\"")==std::string::npos);
    choice_json.command.action=RosterAction::upgrade_choose;choice_json.action_succeeded=true;choice_json.choice_result={true,true};
    check("choice response claims callback completion not game application",result_json(choice_json,0).find("\"state\":\"callback_returned\"")!=std::string::npos&&result_json(choice_json,0).find("\"invocationReturned\":true")!=std::string::npos);
    for(const auto* value:{"upgradeHook:1235","upgradeHookInstall:1235","upgradeHookRemove:1235","upgradeHookInstall:1234:0","upgradeHookRemove:1234\ninspect"})
        check(value,!parse_command(value,1234,command));
    check("upgrade hook install has distinct mutation route",parse_command("upgradeHookInstall:1234",1234,command)&&command.action==RosterAction::upgrade_hook_install);
    check("upgrade hook query remains distinct",parse_command("upgradeHook:1234",1234,command)&&command.action==RosterAction::upgrade_hook_inspect&&!mutates_roster(command.action));
    check("parsed controller options preserve nondefault IDs",parse_command("controlOpen:1234:7:17:23:2:9",1234,command)&&
        command.action==RosterAction::control_open&&command.control.player_id==1&&command.control.source_id==7&&command.control.category_id==17&&command.control.layout_id==23&&command.control.horizontal_axis_id==2&&command.control.vertical_axis_id==9);
    check("parsed pulse preserves axes and bound",parse_command("controlPulse:1234:0.5:-0.25:250",1234,command)&&command.action==RosterAction::control_pulse&&command.x==0.5f&&command.y==-0.25f&&command.duration==250);
    {
        wchar_t previous[128]{};
        const DWORD length=GetEnvironmentVariableW(L"WSM_COOP_CONTROL_OPT_IN",previous,128);
        if(length>=128)return 1;
        for(const auto* flag:{static_cast<const wchar_t*>(nullptr),L"1"}) {
            check("set pulse permission for admission test",SetEnvironmentVariableW(L"WSM_COOP_CONTROL_OPT_IN",flag)!=FALSE);
            ProbeContext refused;refused.command=command;
            execute_command(&refused);
            check("pulse refuses before runtime access even with opt-in",!refused.action_succeeded&&refused.action_error=="Input pulse disabled: persistent cleanup and retry owner unavailable"&&!refused.control);
        }
        check("restore pulse permission",SetEnvironmentVariableW(L"WSM_COOP_CONTROL_OPT_IN",length?previous:nullptr)!=FALSE);
        ProbeContext existing;existing.command=command;
        existing.control=std::make_unique<RewiredControllerSession>();
        auto* owner=existing.control.get();
        for(int retry=0;retry<2;++retry) {
            existing.action_succeeded=true;
            execute_command(&existing);
            const auto response=result_json(existing,1);
            check("repeated pulse refusals retain controller owner and report no write",existing.control.get()==owner&&!existing.action_succeeded&&
                response.find("\"actionSucceeded\":false")!=std::string::npos&&response.find("\"nonzeroWritten\":false")!=std::string::npos&&
                response.find("\"neutralized\":false")!=std::string::npos);
        }
    }
    for(const auto* value:{"controlEnvironment:1235","controlClose:1235","controlOpen:1235:7:17:23:2:9","controlPulse:1235:1:0:120",
        "controlOpen:1234:-1:17:23:2:9","controlOpen:1234:7:17:23:2:2","controlOpen:1234:7:17:23:2:9:0",
        "controlPulse:1234:nan:0:120","controlPulse:1234:inf:0:120","controlPulse:1234:1.01:0:120","controlPulse:1234:0:0:120",
        "controlPulse:1234:1:0:0","controlPulse:1234:1:0:251","controlPulse:1234:1:0:-1","controlClose:1234\nrollback:1234"})
        check(value,!parse_command(value,1234,command));
    for(const auto* value:{"inputProfile:","inputProfile:-1234","inputProfile:1234:0","inputProfile:1234\nrollback:1234"})
        check(value,!parse_command(value,1234,profile_command));
    for(const auto* value:{"equipment:","equipment:-1234","equipment:1234:0","equipment:1234\nrollback:1234"})
        check(value,!parse_command(value,1234,equipment_command));
    for(const auto* text:{"upgradeHint:","upgradeHint:-1234","upgradeHint:1234:0","upgradeHint:1234\nrollback:1234"})
        check(text,!parse_command(text,1234,hint_command));
    for(const auto* text:{"upgradeItems:","upgradeItems:-1234","upgradeItems:1234:0","upgradeItems:1234\nrollback:1234"})
        check(text,!parse_command(text,1234,item_command));
    for(const auto* text:{"upgradeSelection:","upgradeSelection:-1234","upgradeSelection:1234:0","upgradeSelection:1234\nrollback:1234"})
        check(text,!parse_command(text,1234,command));
    check("upgrade query cannot enter mutation dispatch", !mutates_roster(RosterAction::upgrade_context) && !mutates_roster(RosterAction::inspect));
    check("all explicit mutation actions remain classified", mutates_roster(RosterAction::enroll) && mutates_roster(RosterAction::rollback) && mutates_roster(RosterAction::select_player) && mutates_roster(RosterAction::copy_character));
    command = {RosterAction::enroll, 41};
    check("wrong PID leaves command unchanged", !parse_command("enroll:1235:0", 1234, command) && command.action == RosterAction::enroll && command.player == 41);
    for (const auto* text : {"enroll:1234:-1", "enroll:1234:2147483648", "enroll:1234:41:0", "enroll:1234", "enroll:1234:", "rollback:1234:0", "rollback:-1234", "inspect\nrollback:1234", "upgradeContext:", "upgradeContext:-1234", "upgradeContext:1234:0", "upgradeContext:1234\nrollback:1234", "unknown"})
        check(text, !parse_command(text, 1234, command));
    check("bounded command", !parse_command(std::string(97, 'x'), 1234, command));
    check("Int32 maximum ID", parse_command("enroll:1234:" + std::to_string((std::numeric_limits<std::int32_t>::max)()), 1234, command));
    ProbeContext c;
    check("set permission for quiescence-policy test",SetEnvironmentVariableW(L"WSM_UPGRADE_PASS_HOOK_OPT_IN",L"1")!=FALSE);
    for(const auto action:{RosterAction::upgrade_hook_install,RosterAction::upgrade_hook_remove}) {
        ProbeContext refused;refused.command.action=action;
        execute_command(&refused);
        check("upgrade mutation fails closed without established quiescence",!refused.action_succeeded&&refused.action_error=="Upgrade hook mutation disabled: no verified quiescent patch window"&&!process_upgrade_hook.state().ownership_retained);
    }
    check("restore permission after quiescence-policy test",SetEnvironmentVariableW(L"WSM_UPGRADE_PASS_HOOK_OPT_IN",previous_hook_length?previous_hook_flag:nullptr)!=FALSE);
    c.command={RosterAction::upgrade_hook_inspect,-1};
    auto hook_json=result_json(c,0);
    check("mapped upgrade guard is independently reported unverified",hook_json.find("\"mappedPatchedMethodVerified\":false")!=std::string::npos);
    check("upgrade hook state is explicit and initially unowned",hook_json.find("\"action\":\"upgradeHook\"")!=std::string::npos&&hook_json.find("\"upgradeHook\":{\"prepared\":false,\"ownershipRetained\":false,\"lastVerifiedApplied\":false,\"protectionPending\":false}")!=std::string::npos);
    c.command={RosterAction::inspect,-1};
    check("ordinary inspection omits upgrade hook claim",result_json(c,0).find("\"upgradeHook\"")==std::string::npos);
    c.status = CoopRosterStatus::Captured;
    c.snapshot.configured_ids = {0, 41}; c.snapshot.enrolled_ids = {0};
    auto text = result_json(c, 1);
    check("captured roster includes actual supplied IDs", text.find("\"configuredIds\":[0,41]") != std::string::npos && text.find("\"enrolledIds\":[0]") != std::string::npos);
    c.status = CoopRosterStatus::Error;
    text = result_json(c, 2);
    check("error suppresses stale roster and player state", text.find("\"configuredIds\":[]") != std::string::npos && text.find("\"enrolledIds\":[]") != std::string::npos && text.find("\"players\":[]") != std::string::npos);
    check("unavailable configuration is explicit", text.find("\"characterConfiguration\":{\"captured\":false}") != std::string::npos);
    c.configuration_captured = true; c.configuration.selected_types = {37, 0}; c.configuration.coop_config_present = true;
    c.action_error = "action \"quoted\"\\path\n\t";
    c.roster_error = "\b\f\r";
    c.configuration_error = std::string(1, '\x01');
    c.player_error = "player observation failed";
    c.upgrade_error = "upgrade \"context\"\\reference\n";
    text = result_json(c, 3);
    check("configuration preserves selected enum values", text.find("\"coopConfigReferencePresent\":true") != std::string::npos && text.find("\"selectedTypes\":[37,0]") != std::string::npos);
    std::cout << "ROSTER_ERROR_JSON " << text << '\n';
    c.command={RosterAction::upgrade_context,-1};c.upgrade_status=PlayerReadStatus::Captured;
    c.upgrade.level_up_page_count=1;c.upgrade.page_game_session_present=true;c.upgrade.active_character_present=true;
    c.upgrade.active_character_slot=1;c.upgrade.level_up_factory_present=true;
    c.upgrade.actors={{0,41,37,true,false,false},{1,52,91,false,true,true}};
    c.upgrade.choice_group_present=true;c.upgrade.choice_group_items_present=true;
    c.upgrade.choice_group_item_references={false,true};
    auto context_json=result_json(c,4);
    check("captured upgrade query serializes reference identity", context_json.find("\"activeCharacterSlot\":1")!=std::string::npos && context_json.find("\"inputId\":52")!=std::string::npos);
    check("captured upgrade query serializes choice group structure", context_json.find("\"choiceContainerGroupReferencePresent\":true")!=std::string::npos &&
          context_json.find("\"choiceContainerItemsReferencePresent\":true")!=std::string::npos &&
          context_json.find("\"choiceContainerItemReferences\":[false,true]")!=std::string::npos);
    std::cout << "ROSTER_UPGRADE_JSON " << context_json << '\n';
    c.upgrade_status=PlayerReadStatus::Error;context_json=result_json(c,5);
    check("upgrade error suppresses stale context", context_json.find("\"upgradeContext\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    std::cout << "ROSTER_UPGRADE_ERROR_JSON " << context_json << '\n';
    c.upgrade_status=PlayerReadStatus::NotReady;context_json=result_json(c,6);
    check("upgrade not ready suppresses stale context", context_json.find("\"upgradeContext\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::inspect,-1};c.upgrade_status=PlayerReadStatus::Captured;context_json=result_json(c,7);
    check("ordinary inspection does not pretend upgrade query ran", context_json.find("\"upgradeContext\":{\"state\":\"not_requested\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::upgrade_selection,-1};c.upgrade_status=PlayerReadStatus::Captured;
    c.upgrade.choice_selection_observed=true;c.upgrade.selected_choice_container_slot=1;
    c.upgrade.choice_group_item_unity_alive={false,true};c.upgrade.choice_group_item_active={false,true};
    auto selection_json=result_json(c,8);
    check("selection query exposes explicit engine observation", selection_json.find("\"choiceSelection\":{\"observed\":true,\"selectedContainerSlot\":1")!=std::string::npos);
    std::cout << "ROSTER_SELECTION_JSON " << selection_json << '\n';
    c.upgrade_status=PlayerReadStatus::Error;selection_json=result_json(c,9);
    check("selection error suppresses stale engine flags", selection_json.find("\"choiceSelection\"")==std::string::npos && selection_json.find("\"upgradeContext\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    c.upgrade_status=PlayerReadStatus::NotReady;selection_json=result_json(c,10);
    check("selection not ready suppresses stale engine flags", selection_json.find("\"choiceSelection\"")==std::string::npos && selection_json.find("\"upgradeContext\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::upgrade_context,-1};c.upgrade_status=PlayerReadStatus::Captured;selection_json=result_json(c,11);
    check("field query never exposes a stale engine observation", selection_json.find("\"choiceSelection\"")==std::string::npos);
    c.command=item_command;c.upgrade_status=PlayerReadStatus::Captured;
    c.upgrade.choice_items_observed=true;c.upgrade.choice_child_count=2;
    c.upgrade.choice_items={{true,true,false,false,false,false,false,0},{true,true,true,true,true,false,true,7002}};
    auto items_json=result_json(c,12);
    check("item query serializes explicit action and ordered content",items_json.find("\"action\":\"upgradeItems\"")!=std::string::npos &&
          items_json.find("\"choiceItems\":{\"observed\":true,\"childCount\":2,\"items\":[")!=std::string::npos &&
          items_json.find("\"weaponType\":7002,\"dataReferencePresent\":false,\"isNew\":true")!=std::string::npos);
    std::cout << "ROSTER_ITEMS_JSON " << items_json << '\n';
    c.upgrade_status=PlayerReadStatus::Error;items_json=result_json(c,13);
    check("item error suppresses all stale observations",items_json.find("\"choiceItems\"")==std::string::npos && items_json.find("\"choiceSelection\"")==std::string::npos &&
          items_json.find("\"upgradeContext\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    c.upgrade_status=PlayerReadStatus::NotReady;items_json=result_json(c,14);
    check("item not ready suppresses all stale observations",items_json.find("\"choiceItems\"")==std::string::npos &&
          items_json.find("\"upgradeContext\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    for(auto action:{RosterAction::upgrade_context,RosterAction::upgrade_selection}) {
        c.command={action,-1};c.upgrade_status=PlayerReadStatus::Captured;items_json=result_json(c,15);
        check("less detailed query never exposes stale item observations",items_json.find("\"choiceItems\"")==std::string::npos);
    }
    const auto directory = std::filesystem::path(argv[1]) / ("roster-command-" + std::to_string(GetCurrentProcessId()));
    c.command={RosterAction::upgrade_hint,-1};c.upgrade_status=PlayerReadStatus::Captured;
    c.upgrade.fourth_option_hint_observed=true;c.upgrade.fourth_option_hint_present=true;
    c.upgrade.fourth_option_hint_unity_alive=true;c.upgrade.fourth_option_hint_comparison_complete=true;
    c.upgrade.fourth_option_hint_child_slot=1;
    auto hint_json=result_json(c,16);
    check("hint query serializes explicit action and identity",hint_json.find("\"action\":\"upgradeHint\"")!=std::string::npos &&
          hint_json.find("\"fourthOptionHint\":{\"observed\":true,\"referencePresent\":true,\"unityAlive\":true,\"childComparisonComplete\":true,\"childSlot\":1}")!=std::string::npos);
    std::cout << "ROSTER_HINT_JSON " << hint_json << '\n';
    c.upgrade_status=PlayerReadStatus::Error;hint_json=result_json(c,17);
    check("hint error suppresses all stale observations",hint_json.find("\"fourthOptionHint\"")==std::string::npos &&
          hint_json.find("\"upgradeContext\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    c.upgrade_status=PlayerReadStatus::NotReady;hint_json=result_json(c,18);
    check("hint not ready suppresses all stale observations",hint_json.find("\"fourthOptionHint\"")==std::string::npos &&
          hint_json.find("\"upgradeContext\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    for(auto action:{RosterAction::upgrade_context,RosterAction::upgrade_selection,RosterAction::upgrade_items}) {
        c.command={action,-1};c.upgrade_status=PlayerReadStatus::Captured;hint_json=result_json(c,19);
        check("less detailed query never exposes stale hint observation",hint_json.find("\"fourthOptionHint\"")==std::string::npos);
    }
    c.command={RosterAction::equipment,-1};c.equipment_status=PlayerReadStatus::Captured;
    EquipmentActorSnapshot inventory;inventory.slot=1;inventory.input_id=52;inventory.character_type=91;
    inventory.manager_present=true;inventory.collection_present=true;
    inventory.items={{true,true,true,true,7002,1,"Weapons.Quoted\"Name"},{false,false,false,false,0,-1,""}};
    c.equipment.actors={inventory};
    auto equipment_json=result_json(c,20);
    check("equipment query serializes own action and owner",equipment_json.find("\"action\":\"equipment\"")!=std::string::npos &&
        equipment_json.find("\"equipment\":{\"state\":\"captured\",\"actors\":[")!=std::string::npos &&
        equipment_json.find("\"ownerSlot\":1")!=std::string::npos);
    std::cout << "ROSTER_EQUIPMENT_JSON " << equipment_json << '\n';
    c.equipment_status=PlayerReadStatus::Error;equipment_json=result_json(c,21);
    check("equipment error suppresses stale inventory",equipment_json.find("\"equipment\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    c.equipment_status=PlayerReadStatus::NotReady;equipment_json=result_json(c,22);
    check("equipment not ready suppresses stale inventory",equipment_json.find("\"equipment\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::inspect,-1};c.equipment_status=PlayerReadStatus::Captured;equipment_json=result_json(c,23);
    check("ordinary inspection never exposes stale equipment",equipment_json.find("\"equipment\":{\"state\":\"not_requested\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::input_profile,-1};c.input_profile_status=PlayerReadStatus::Captured;
    c.input_profile.manager_count=1;c.input_profile.horizontal_action_id=101;c.input_profile.vertical_action_id=103;
    c.input_profile.definitions={{7,{2,9}},{11,{}}};
    auto profile_json=result_json(c,24);
    check("input profile serializes definitions and actual actions",profile_json.find("\"action\":\"inputProfile\"")!=std::string::npos&&
        profile_json.find("\"inputProfile\":{\"state\":\"captured\",\"managerCount\":1,\"horizontalActionId\":101,\"verticalActionId\":103,\"definitions\":[{\"sourceId\":7,\"axisIds\":[2,9]},{\"sourceId\":11,\"axisIds\":[]}]}")!=std::string::npos);
    std::cout << "ROSTER_INPUT_PROFILE_JSON " << profile_json << '\n';
    c.input_profile_status=PlayerReadStatus::Error;profile_json=result_json(c,25);
    check("input profile error suppresses stale definitions",profile_json.find("\"inputProfile\":{\"state\":\"error\"}")!=std::string::npos);
    c.input_profile_status=PlayerReadStatus::NotReady;profile_json=result_json(c,26);
    check("input profile not ready suppresses stale definitions",profile_json.find("\"inputProfile\":{\"state\":\"not_ready\"}")!=std::string::npos);
    c.command={RosterAction::inspect,-1};c.input_profile_status=PlayerReadStatus::Captured;profile_json=result_json(c,27);
    check("ordinary inspection never exposes stale input profile",profile_json.find("\"inputProfile\":{\"state\":\"not_requested\"}")!=std::string::npos);
    c.command={RosterAction::control_environment,-1};c.control_status=PlayerReadStatus::Captured;
    c.control_environment.horizontal_action_id=101;c.control_environment.vertical_action_id=103;
    c.control_environment.categories={{17,"Gameplay"}};c.control_environment.layouts={{23,"CustomDefault"}};
    auto controls_json=result_json(c,28);
    check("explicit controller environment serializes typed nondefault IDs",controls_json.find("\"action\":\"controlEnvironment\"")!=std::string::npos&&controls_json.find("\"horizontalActionId\":101,\"verticalActionId\":103,\"categories\":[{\"id\":17,\"name\":\"Gameplay\"}]")!=std::string::npos);
    c.control_status=PlayerReadStatus::Error;controls_json=result_json(c,29);
    check("controller error suppresses stale environment",controls_json.find("\"controlEnvironment\":{\"state\":\"error\"}")!=std::string::npos);
    c.command={RosterAction::inspect,-1};c.control_status=PlayerReadStatus::Captured;controls_json=result_json(c,30);
    check("unrequested controller query suppresses stale environment",controls_json.find("\"controlEnvironment\":{\"state\":\"not_requested\"}")!=std::string::npos);
    c.command={RosterAction::control_sources,-1};c.control_status=PlayerReadStatus::Captured;
    c.control_environment.custom_sources_captured=true;
    RewiredPlayerInputState source_player;source_player.player_id=1;
    RewiredMapState source_map;source_map.id=401;source_map.controller_id=400;source_map.controller_type=20;
    source_map.category_id=17;source_map.layout_id=23;source_map.enabled=true;source_player.maps.push_back(source_map);
    source_player.custom_sources={{400,7,{2,9},{401}},{402,11,{}, {}}};
    c.control_environment.players={source_player};
    auto sources_json=result_json(c,31);
    check("custom source diagnostic serializes runtime identity separately from definition",sources_json.find("\"action\":\"controlSources\"")!=std::string::npos &&
          sources_json.find("\"customSourcesCaptured\":true")!=std::string::npos &&
          sources_json.find("\"customSources\":[{\"controllerId\":400,\"sourceId\":7,\"axisIds\":[2,9],\"mapIds\":[401]},{\"controllerId\":402,\"sourceId\":11,\"axisIds\":[],\"mapIds\":[]}]")!=std::string::npos);
    std::cout << "ROSTER_CUSTOM_SOURCES_JSON " << sources_json << '\n';
    c.control_status=PlayerReadStatus::Error;sources_json=result_json(c,32);
    check("failed custom source query suppresses stale identity",sources_json.find("\"controlEnvironment\":{\"state\":\"error\"}")!=std::string::npos && sources_json.find("\"customSources\":")==std::string::npos);
    c.control_status=PlayerReadStatus::Captured;c.command.action=RosterAction::control_environment;sources_json=result_json(c,33);
    check("ordinary environment never exposes opt-in source getter observations",sources_json.find("\"customSourcesCaptured\":")==std::string::npos && sources_json.find("\"customSources\":")==std::string::npos);
    c.command.action=RosterAction::input_zero_start;c.zero_profile_attempted=c.zero_environment_attempted=true;
    c.input_profile_status=PlayerReadStatus::Captured;c.action_error="Zero trial source definition is ambiguous";
    auto admission_json=result_json(c,34);
    check("failed zero selection preserves actually observed admission catalogs",admission_json.find("\"inputProfile\":{\"state\":\"captured\"")!=std::string::npos &&
          admission_json.find("\"controlEnvironment\":{\"state\":\"captured\"")!=std::string::npos && admission_json.find("\"customSources\":")==std::string::npos);
    std::cout << "ROSTER_ZERO_ADMISSION_JSON " << admission_json << '\n';
    c.zero_environment_attempted=false;admission_json=result_json(c,35);
    check("unattempted zero admission environment never exposes earlier snapshot",admission_json.find("\"controlEnvironment\":{\"state\":\"not_requested\"}")!=std::string::npos);
    c.input_profile_status=PlayerReadStatus::Error;admission_json=result_json(c,36);
    check("failed zero admission profile suppresses stale candidate definitions",admission_json.find("\"inputProfile\":{\"state\":\"error\"}")!=std::string::npos && admission_json.find("\"definitions\":")==std::string::npos);
    c.command.action=RosterAction::input_zero_state;c.input_profile_status=PlayerReadStatus::Captured;admission_json=result_json(c,37);
    check("zero status does not imply a fresh admission catalog",admission_json.find("\"inputProfile\":{\"state\":\"not_requested\"}")!=std::string::npos);
    ProbeContext source_guard;source_guard.command.action=RosterAction::control_sources;
    source_guard.control_status=PlayerReadStatus::Captured;source_guard.control_environment=c.control_environment;
    execute_command(&source_guard);
    check("custom source guard refusal invalidates previous captured metadata",!source_guard.action_succeeded && source_guard.control_status==PlayerReadStatus::Error &&
          result_json(source_guard,38).find("\"controlEnvironment\":{\"state\":\"error\"}")!=std::string::npos);
    for(const auto failure:{HookResult::failed,HookResult::timeout,HookResult::busy}) {
        ProbeContext sequence;sequence.command.action=RosterAction::input_zero_start;
        sequence.zero_profile_attempted=sequence.zero_environment_attempted=true;
        sequence.input_profile_status=sequence.control_status=PlayerReadStatus::Captured;
        sequence.input_profile=c.input_profile;sequence.control_environment=c.control_environment;
        const auto previous=result_json(sequence,39);
        check("sequential refusal begins with previously captured admission metadata",previous.find("\"inputProfile\":{\"state\":\"captured\"")!=std::string::npos &&
              previous.find("\"controlEnvironment\":{\"state\":\"captured\"")!=std::string::npos);
        // The real worker clears values/statuses before dispatch, not the old attempt flags.
        sequence.input_profile={};sequence.control_environment={};
        sequence.input_profile_status=sequence.control_status=PlayerReadStatus::Error;
        dispatch_injected=failure;
        const auto status=dispatch_roster_command(GetModuleHandleW(nullptr),sequence,unavailable_dispatch);
        const auto next=result_json(sequence,40);
        check("zero unexecuted dispatch resets prior per-read attempts",status==failure && !sequence.zero_profile_attempted && !sequence.zero_environment_attempted &&
              next.find("\"inputProfile\":{\"state\":\"not_requested\"}")!=std::string::npos && next.find("\"controlEnvironment\":{\"state\":\"not_requested\"}")!=std::string::npos);
    }
    ProbeContext source_failure;source_failure.command.action=RosterAction::control_sources;
    source_failure.control_environment=c.control_environment;source_failure.control_status=PlayerReadStatus::Captured;source_failure.action_succeeded=true;
    try { throw std::runtime_error("Injected later roster inspection failure"); }
    catch(const std::exception& e) { fail_roster_callback(source_failure,e.what()); }
    const auto source_failure_json=result_json(source_failure,41);
    check("callback failure masks optional sources captured before a later exception",!source_failure.action_succeeded && source_failure.control_status==PlayerReadStatus::Error &&
          source_failure_json.find("\"controlEnvironment\":{\"state\":\"error\"}")!=std::string::npos && source_failure_json.find("\"customSources\":")==std::string::npos);
    ProbeContext admission_failure;admission_failure.command.action=RosterAction::input_zero_start;
    admission_failure.zero_profile_attempted=admission_failure.zero_environment_attempted=true;
    admission_failure.input_profile=c.input_profile;admission_failure.input_profile_status=PlayerReadStatus::Captured;
    admission_failure.control_status=PlayerReadStatus::Error;
    fail_roster_callback(admission_failure,"Injected environment read failure");
    auto admission_failure_json=result_json(admission_failure,42);
    check("later callback exception preserves completed zero profile capture",admission_failure.input_profile_status==PlayerReadStatus::Captured &&
          admission_failure_json.find("\"inputProfile\":{\"state\":\"captured\"")!=std::string::npos && admission_failure_json.find("\"controlEnvironment\":{\"state\":\"error\"}")!=std::string::npos);
    admission_failure.input_profile_status=admission_failure.control_status=PlayerReadStatus::Captured;
    admission_failure.control_environment=c.control_environment;
    fail_roster_callback(admission_failure,"Injected later owner preparation failure");
    admission_failure_json=result_json(admission_failure,43);
    check("owner preparation exception preserves both completed zero admission reads",admission_failure_json.find("\"inputProfile\":{\"state\":\"captured\"")!=std::string::npos &&
          admission_failure_json.find("\"controlEnvironment\":{\"state\":\"captured\"")!=std::string::npos);
    admission_failure.input_profile_status=PlayerReadStatus::NotReady;admission_failure.zero_environment_attempted=false;
    fail_roster_callback(admission_failure,"Injected later diagnostic failure");
    check("callback failure preserves completed not-ready admission result",admission_failure.input_profile_status==PlayerReadStatus::NotReady &&
          result_json(admission_failure,44).find("\"inputProfile\":{\"state\":\"not_ready\"}")!=std::string::npos);
    admission_failure.input_profile_status=PlayerReadStatus::Error;
    fail_roster_callback(admission_failure,"Injected exception during profile read");
    check("incomplete admission read remains error without stale definitions",result_json(admission_failure,45).find("\"inputProfile\":{\"state\":\"error\"}")!=std::string::npos &&
          result_json(admission_failure,45).find("\"definitions\":")==std::string::npos);
    if (!std::filesystem::create_directory(directory)) return 2;
    try {
        const auto request = directory / "request";
        { std::ofstream out(request, std::ios::binary); out << "inspect"; }
        check("bounded request read", read_command(request) == "inspect");
        const auto output = directory / "state.json";
        publish_result(output, text);
        bool refused = false;
        try { publish_result(output, "replacement"); } catch (const std::exception&) { refused = true; }
        std::ifstream in(output, std::ios::binary);
        const std::string actual(std::istreambuf_iterator<char>{in}, {});
        check("published output cannot be replaced", refused && actual == text);
        in.close();
        std::filesystem::remove_all(directory);
    } catch (...) { std::filesystem::remove_all(directory); throw; }
    std::cout << "Roster command checks: " << count - failed << " passed, " << failed << " failed, " << count << " total\n";
    return failed ? 1 : 0;
}
